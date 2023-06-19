/*
 * @Author: Yixuan Meng yixuanm@seas.upenn.edu
 * @Date: 2023-05-02 16:43:58
 * @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 * @LastEditTime: 2023-05-08 18:52:49
 * @FilePath: /T03/backend/backend_master.cc
 * @Description: This file containss the helper functions for the master node (backend)
 */

#include "kvs.h"
#include "../common/common_functions.h"

using namespace std;

bool verbose_output = true;
vector<pthread_t> threads;
vector<int> client_socket;
int total_backend_server_number;

unordered_map<string, int> all_kvs_mapping;
pthread_mutex_t mapping_lock;
vector<bool> backend_status;

// Define the struct for passing arguments to the client handler thread
typedef struct
{
    void *fd;
    vector<server_data> *server_data_list;
    bool verbose_output;
} client_handler_args_master_node;

// Define the struct for passing arguments to the heartbeat thread
typedef struct
{
    vector<server_data> *server_data_list;
    bool verbose_output;
} heartbeat_args;

vector<bool> get_backend_status(vector<server_data> server_data_list);

/**
 * @description:this function handles the SIGPIPE signal
 * @param {int} signum
 * @return {*}
 */
void sigpipe_handler(int signum)
{
    // handle SIGPIPE signal
    signal(SIGPIPE, SIG_IGN);
}

/**
 * @description: this function is used to get the status of all backend servers
 * @param {vector<server_data>} server_data_list
 * @return {*}
 */
void inspect_backend_status(vector<server_data> server_data_list)
{
    for (int i = 1; i < server_data_list.size(); i++)
    {
        if (backend_status[i] == true)
        {
            cout << "Backend server " << i << " is alive" << endl;
        }
        else
        {
            cout << "Backend server " << i << " is dead" << endl;
        }
    }
}

/**
 * @description: this function is used to get the index of the server in the server_data_list by its port number
 * @param {vector<server_data>} server_data_list
 * @param {int} portno
 * @return {*}
 */
int get_server_idx_by_port(vector<server_data> server_data_list, int portno)
{
    for (int i = 0; i < server_data_list.size(); i++)
    {
        if (server_data_list[i].forward_portno == portno)
            return i;
    }
    return -1;
}

/**
 * @description: this function is used to get valid backend server fds
 * @param {vector<server_data>} server_data_list
 * @param {int} grp_idx
 * @return {*}
 */
vector<int> get_backend_socks(vector<server_data> server_data_list, int grp_idx)
{
    vector<int> server_fds;
    int grp_start = grp_idx * num_server_in_group + 1;
    for (int i = grp_start; i < grp_start + num_server_in_group; i++)
    {
        server_data s = server_data_list[i];
        sockaddr_in server_src = s.forward_src;
        int sfd = socket(PF_INET, SOCK_STREAM, 0);
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
        server_addr.sin_port = htons(s.forward_portno);

        int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        // get the alive server fds so that master can assign them to the frontend
        if (ret == 0)
        {
            char buffer[1000];
            char message[1000];
            sprintf(message, "%s", "status\r\n");
            send(sfd, message, strlen(message), 0);
            ssize_t num_bytes = recv(sfd, buffer, sizeof buffer, 0);
            buffer[num_bytes] = '\0';
            bool res = 0;
            string str(buffer);
            memset(buffer, 0, sizeof buffer);
            size_t pos = str.find("+OK ");
            if (pos != string::npos)
            {
                res = stoi(str.substr(pos + 4, pos + 5));
            }
            // send quit command to the server to close the connection
            sprintf(message, "%s", "quit\r\n");
            send(sfd, message, strlen(message), 0);
            if (res == 1)
                server_fds.push_back(s.forward_portno);
        }
    }
    return server_fds;
}

/**
 * @description: inspect the mapping from user to backend server
 * @param {unordered_map<string, int>} all_kvs_mapping
 * @return {*}
 */
void inspect_all_kvs_mapping(unordered_map<string, int> all_kvs_mapping)
{
    cerr << "all_kvs_mapping: " << endl;
    for (auto it = all_kvs_mapping.begin(); it != all_kvs_mapping.end(); it++)
    {
        cerr << it->first << ":";
        cerr << it->second << "\n";
    }
}

/**
 * @description: this function is executed by a thread when a new connection is established
 * take care of the communication between the client and the master node
 * assign alive server to the frontend (getserver command) or respond with heartbeat when requested
 * @param {void} *args
 * @return {*}
 */
void *client_handler_master_node(void *args)
{
    signal(SIGPIPE, sigpipe_handler);
    client_handler_args_master_node *ch_args = (client_handler_args_master_node *)args;
    void *fd = ch_args->fd;
    int comm_fd = *(int *)fd;
    bool verbose_output = ch_args->verbose_output;

    vector<server_data> server_data_list = *(vector<server_data> *)ch_args->server_data_list;

    char buffer[1000000];
    char buffer_recv_from_server[1000000];
    memset(buffer_recv_from_server, 0, sizeof(buffer_recv_from_server));
    char message[1000000];
    string message_to_server;

    bool client_quit = false;
    bool send_same_command_to_backend_server = false;
    bool multicast_the_command = false;
    int n;
    char *command;    // find the occurrence of "\r\n"
    char *p = buffer; // keep track of the current position in the buffer

    string response_to_client;
    string response_from_server;
    int num_bytes;

    while (1)
    {
        send_same_command_to_backend_server = false;
        n = read(comm_fd, p, sizeof buffer - strlen(buffer));
        if (n == 0)
        {
            continue;
        }

        vector<int> backend_server_store_index; // index of the backend server that store the kv pair
        int hashed_mod;

        while ((command = strstr(buffer, "\r\n")) != NULL)
        {
            *command = '\0';
            command += 2;

            // getserver[sep]username\r\n - frontend servers send this command to the master node to get a backend server
            if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "getserver[sep]", 14) == 0)
            {
                vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
                if (fields.size() != 2)
                {
                    response_to_client = "-ERR Illegal Argyment of getserver(username)\r\n";
                    sprintf(message, "%s", response_to_client.c_str());
                }
                else
                {
                    int group_num = (server_data_list.size() - 1) / num_server_in_group;
                    if ((server_data_list.size() - 1) % num_server_in_group != 0)
                        group_num++;
                    if (!all_kvs_mapping.count(fields[1]))
                    {
                        // index of assigned group, computed by hashing
                        hashed_mod = CommonFunctions::hash_string(fields[1], group_num);
                    }
                    else
                    {
                        hashed_mod = all_kvs_mapping[fields[1]];
                        if (pthread_mutex_trylock(&mapping_lock) == 0)
                        {
                            all_kvs_mapping[fields[1]] = hashed_mod;
                            if (verbose_output)
                                cout << "added " << fields[1] << ":" << hashed_mod << "to all_kvs_mapping" << endl;
                            pthread_mutex_unlock(&mapping_lock);
                        }
                    }

                    // valid servers in the group
                    vector<int> valid_ports = get_backend_socks(server_data_list, hashed_mod);
                    // index of server in the assigned group
                    hashed_mod = CommonFunctions::hash_string(fields[1], valid_ports.size());
                    if (verbose_output)
                        cerr << "selected server at port " << valid_ports[hashed_mod] << endl;

                    hashed_mod = valid_ports[hashed_mod];

                    int server_idx = get_server_idx_by_port(server_data_list, hashed_mod);

                    response_to_client = "+OK available server #" + to_string(server_idx) + " at port " + to_string(hashed_mod) + "\r\n";
                }
                sprintf(message, "%s", response_to_client.c_str());
            }
            else if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "heartbeat") == 0)
            {
                backend_status = get_backend_status(server_data_list);
                sprintf(message, "+OK %s\r\n", CommonFunctions::join(backend_status, ",").c_str()); // send to frontend
            }
            else if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "quit") == 0)
            {
                sprintf(message, GOODBYE_MESSAGE);
                client_quit = true;
            }
            else
            {
                sprintf(message, UNK_COMMAND_MESSAGE);
            }

            send(comm_fd, message, strlen(message), 0); // send to client

            if (verbose_output)
                cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer);
            if (verbose_output)
                cerr << "[" << comm_fd << "] S: " << message;
            if (client_quit)
                break;
            int len = strlen(command);
            memmove(buffer, command, len + 1);
            // Clear everything before the pointer
            memset(buffer + len, 0, command - buffer);
        }
        p = buffer;
        while (*p != '\0')
            p++;
        // quit
        if (client_quit)
            break;
    }
    close(comm_fd);
    if (verbose_output)
        cerr << "[" << comm_fd << "] "
             << "Connection closed\r\n";
    pthread_exit(NULL);
    return NULL;
}

/**
 * @description: executed regularly to check status of each backend server by the heartbeat thread
 * @param {vector<server_data>} server_data_list
 * @return {*}
 */
vector<bool> get_backend_status(vector<server_data> server_data_list)
{
    // define backend_status
    vector<bool> backend_status;
    backend_status.push_back(true);

    for (int i = 1; i < server_data_list.size(); i++)
    {
        server_data s = server_data_list[i];
        sockaddr_in server_src = s.forward_src;
        int sfd = socket(PF_INET, SOCK_STREAM, 0);
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
        server_addr.sin_port = htons(s.forward_portno);

        int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret == 0)
        {
            // if send status and get +OK 1\r\n -> alive
            char buffer[1000000];
            char message[1000000];
            sprintf(message, "%s", "status\r\n");
            send(sfd, message, strlen(message), 0);

            ssize_t num_bytes = recv(sfd, buffer, sizeof buffer, 0);
            buffer[num_bytes] = '\0';

            bool res;
            string str(buffer);
            memset(buffer, 0, sizeof buffer);
            size_t pos = str.find("+OK ");
            // server dead or alive
            if (pos != string::npos)
            {
                res = bool(stoi(str.substr(pos + 4, pos + 5)));
            }
            else
            {
                res = false;
            }
            sprintf(message, "%s", "quit\r\n");
            send(sfd, message, strlen(message), 0);
            backend_status.push_back(res);
        }
        else
        {
            backend_status.push_back(false);
        }
        close(sfd);
    }

    if (verbose_output)
        cout << "backend status: " << CommonFunctions::join(backend_status, ",") << endl;
    return backend_status;
}

/**
 * @description: this function is executed by the heartbeat thread,
 * check heartbeat of each backend server every two seconds
 * @param {void} *args
 * @return {*}
 */
void *heartbeat_func(void *args)
{
    heartbeat_args *ch_args = (heartbeat_args *)args;
    bool verbose_output = ch_args->verbose_output;
    vector<server_data> server_data_list = *(vector<server_data> *)ch_args->server_data_list;

    while (1)
    {
        backend_status = get_backend_status(server_data_list);
        sleep(2);
    }

    pthread_exit(NULL);
    return NULL;
}

/**
 * @description: this function takes new connections and creates new threads to handle them
 * @param {int} portno
 * @param {int} server_idx
 * @param {bool} verbose_output
 * @param {vector<server_data>} server_data_list
 * @return {*}
 */
void KVS_ns::run_backend_master(int portno, int server_idx, bool verbose_output, vector<server_data> server_data_list)
{
    total_backend_server_number = server_data_list.size() - 1; // assuming the first config is master node

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        if (verbose_output)
            cerr << "can't open socket" << endl;
        exit(1);
    }
    client_socket.push_back(listen_fd);
    if (verbose_output)
        cerr << "Put " << listen_fd << " into client_socket" << endl;

    // reuse of address and port
    const int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    // set to port number
    servaddr.sin_port = htons(portno);
    bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    // connect to the server
    listen(listen_fd, 110);
    if (verbose_output)
        cerr << "Running on port " << portno << endl;

    // heartbeat
    backend_status = get_backend_status(server_data_list);
    if (verbose_output)
        inspect_backend_status(server_data_list);
    pthread_t thread;
    threads.push_back(thread);
    heartbeat_args args = {.server_data_list = &server_data_list,
                           .verbose_output = verbose_output};
    pthread_create(&thread, NULL, &heartbeat_func, &args);

    // connect to client
    while (true)
    {
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);
        int comm_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
        if (comm_fd == -1)
            break;
        client_socket.push_back(comm_fd);
        if (verbose_output)
            cerr << "Connection from " << inet_ntoa(clientaddr.sin_addr) << ":" << clientaddr.sin_port << endl;
        pthread_t thread;
        threads.push_back(thread);

        client_handler_args_master_node args = {.fd = &comm_fd,
                                                .server_data_list = &server_data_list,
                                                .verbose_output = verbose_output};
        pthread_create(&thread, NULL, &client_handler_master_node, &args);
    }
}
