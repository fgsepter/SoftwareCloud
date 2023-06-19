#include "load_balancer.h"
#include "../common/common_functions.h"

bool vflag = false;
int listen_fd;                                         // listen from frontend servers
string config_file = "../common/frontend_servers.txt"; // load balancer: index = 0, other frontend servers: index > 0
vector<server_data> server_list;                       // all the frontend servers, including load balancer
vector<int> frontend_status;
int redirect_server_port; // current frontend server who has the minimum number of connections
int min_connections;      // mininum number of connections of all the frontend servers
server_data load_balancer;
int MAX_CONNECTIONS = 1000;

struct args
{
    int ClientFD;
};

struct heartbeat_args
{
    vector<server_data> server_list;
};

/**
 * Function to print debug output
 * @author Zhaozheng Shen
 */
void Debug_Print(string info)
{
    if (vflag)
    {
        cout << info;
    }
}

/**
 * Handle command line input
 * @author Zhaozheng Shen
 */
void HandleCommandLine(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "v")) != -1)
    {
        switch (opt)
        {
        case 'v':
        {
            vflag = true;
            break;
        }
        default:
        {
            cerr << "Usage: "
                 << " [-v]" << endl;
            cerr << "Author: Zhaozheng Shen, "
                 << "SEAS login: shenzhzh" << endl;
        }
        }
    }
}

/**
 * Communication function to receive a single message from ClientFD ending with \r\n
 * @author Zhaozheng Shen
 */
string Receive_Message(int ClientFD)
{
    string message;
    /*** Receive message and data from client ***/
    char prev[1];
    while (true)
    {
        char buf[1];
        int n = recv(ClientFD, buf, 1, 0);
        if (buf[0] == '\n' and prev[0] == '\r')
            break;
        message += buf[0];
        prev[0] = buf[0];
        if (n <= 0)
        {
            return message;
        }
        if ((int)message.length() > MAX_LENGTH)
        {
            throw "Message too long";
        }
    }
    // Debug_Print("[" + to_string(ClientFD) + "] C: " + message + "\n");
    return message.substr(0, message.length() - 1);
}

/**
 * Communication function to send a single message to ClientFD ending with \r\n
 * @author Zhaozheng Shen
 */
void Send_Message(int ClientFD, string message_str)
{
    char message[MAX_LENGTH];
    size_t length;
    length = message_str.length();
    strcpy(message, message_str.c_str());
    send(ClientFD, message, length, MSG_NOSIGNAL);
    // Debug_Print("[" + to_string(ClientFD) + "] S: " + message_str + "\n");
}

/**
 * Receive header lines from HTTP request
 * @author Zhaozheng Shen
 */
map<string, string> Receive_Headers(int comm_fd)
{
    map<string, string> headers;
    while (true)
    {
        // TODO: handle the header lines properly
        string header = Receive_Message(comm_fd);
        size_t colon_pos = header.find(':');
        if (colon_pos != string::npos)
        {
            string header_name = header.substr(0, colon_pos);
            string header_value = header.substr(colon_pos + 1);
            headers[header_name] = header_value;
        }
        if (header == "") /* Receive an empty line */
            break;
    }
    return headers;
}

/**
 * Thread function for multi-thread implementation, each thread resolves one connection from client
 * @author Zhaozheng Shen
 */
void *Thread_Running(void *arg)
{
    int comm_fd = ((struct args *)arg)->ClientFD;
    while (true)
    {
        try
        {
            string client_request = Receive_Message(comm_fd);
            string response;

            if (client_request == "")
            {
                close(comm_fd);
                return NULL;
            }
            if (client_request == "heartbeat")
            {
                frontend_status = get_frontend_status();
                response += "+OK ";
                for (int i = 0; i < frontend_status.size(); i++)
                {
                    if (frontend_status[i] < 0)
                        response += "0";
                    else
                        response += "1";
                    if (i != frontend_status.size() - 1)
                        response += ",";
                }
                // cout << response << endl;
                response += "\r\n";
                Send_Message(comm_fd, response);
            }
            else
            {
                map<string, string> headers = Receive_Headers(comm_fd);
                stringstream ss(client_request);
                string buf, url, version, response_msg;
                ss >> buf >> url >> version;

                frontend_status = get_frontend_status();
                response += version + " 301\r\n";
                response += "Content-type: text/html\r\n";
                response += "Content-length: 0\r\n";
                response += "Location: http://localhost:" + to_string(redirect_server_port) + url + "\r\n";
                response += "\r\n";
                // cout << "Thread running ends" << endl;
                Send_Message(comm_fd, response);
                close(comm_fd);
                return NULL;
            }
        }
        catch (...)
        {
            // cout << "Thread running failed" << endl;
            close(comm_fd);
            return NULL;
        }
    }
    close(comm_fd);
    return NULL;
}

/**
 * Get frontend_status. -1 if cannot connect to frontend; >= 0 if can connect, and
 * the value is the number of persistent connections.
 * @author Zhaozheng Shen
 */
vector<int> get_frontend_status()
{
    vector<int> frontend_status;
    frontend_status.push_back(0);
    redirect_server_port = -1;
    min_connections = MAX_CONNECTIONS;

    for (int i = 1; i < server_list.size(); i++)
    {
        struct sockaddr_in frontendaddr;
        memset(&frontendaddr, 0, sizeof(frontendaddr));
        frontendaddr.sin_family = AF_INET;
        frontendaddr.sin_port = htons(server_list[i].forward_portno);
        frontendaddr.sin_addr.s_addr = inet_addr(server_list[i].forward_IPaddr);
        int sfd = socket(PF_INET, SOCK_STREAM, 0);

        if (connect(sfd, (struct sockaddr *)&frontendaddr, sizeof(frontendaddr)) == 0)
        {
            // if send status and get +OK 1\r\n -> alive
            string message = "LOAD BALANCER\r\n";
            Send_Message(sfd, message);
            string buffer = Receive_Message(sfd);
            int cur_connections;
            stringstream ss(buffer);
            string buf, connection;
            ss >> buf >> connection;
            // cout << server_list[i].forward_portno << ":" << connection << endl;
            if (connection == "-1")
            {
                frontend_status.push_back(-1);
                continue;
            }
            cur_connections = stoi(connection);
            frontend_status.push_back(cur_connections);
            if (cur_connections >= 0 && min_connections > cur_connections)
            {
                redirect_server_port = server_list[i].forward_portno;
                min_connections = cur_connections;
            }
        }
        else
            frontend_status.push_back(-1);
    }
    // cout << "to connect:" << redirect_server_port << ", number:" << min_connections << endl;
    return frontend_status;
}

/**
 * Defind the heartbeat functions. load balacer ask the frontend staus every 2 seconds
 * @author Zhaozheng Shen
 */
void *heartbeat_func(void *arg)
{
    while (true)
    {
        frontend_status = get_frontend_status();
        if (vflag)
        {
            for (int i = 1; i < server_list.size(); i++)
            {
                if (frontend_status[i] >= 0)
                    cout << "Frontend server " << i << " is alive" << endl;
                else
                    cout << "Frontend server " << i << " is dead" << endl;
            }
            cout << endl;
        }
        sleep(2);
    }

    pthread_exit(NULL);
    return NULL;
}

/**
 * Open the load balancer and begins to listen client requests from users
 * @author Zhaozheng Shen
 */
void CreateLoadBalancer()
{
    struct sockaddr_in servaddr;
    listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        fprintf(stderr, "Cannot create socket: %s\n", strerror(errno));
        return;
    }
    int val = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &val, sizeof(val)) < 0)
    {
        fprintf(stderr, "Cannot set socket options: %s\n", strerror(errno));
        return;
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(load_balancer.bind_portno);

    if (bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        fprintf(stderr, "Fail to bind: %s\n", strerror(errno));
        return;
    }
    socklen_t length = sizeof(servaddr);
    if (getsockname(listen_fd, (struct sockaddr *)&servaddr, &length) < 0)
    {
        fprintf(stderr, "Error getting socket name: %s\n", strerror(errno));
        return;
    }
    if (listen(listen_fd, 100) < 0)
    {
        fprintf(stderr, "Listen failed: %s\n", strerror(errno));
        return;
    }
    Debug_Print("Load balancer started on port number " + to_string(load_balancer.bind_portno) + "\n");

    // check frontend server status
    pthread_t heartbeat_thread;
    pthread_create(&heartbeat_thread, NULL, heartbeat_func, NULL);

    // connect to client
    while (true)
    {
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);
        int comm_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
        if (comm_fd < 0)
        {
            fprintf(stderr, "Accept failed: %s\n", strerror(errno));
            return;
        }
        Debug_Print("[" + to_string(comm_fd) + "] New connection\r\n");

        // check frontend server status
        pthread_t client_thread;
        struct args *input = (struct args *)malloc(sizeof(struct args));
        input->ClientFD = comm_fd;
        pthread_create(&client_thread, NULL, Thread_Running, (void *)input);
    }
    close(listen_fd);
}

/**
 * Main function defined here
 * @author Zhaozheng Shen
 */
int main(int argc, char *argv[])
{
    HandleCommandLine(argc, argv);
    server_list = CommonFunctions::process_config_file(config_file, 0);
    load_balancer = server_list[0];

    CreateLoadBalancer();

    return 0;
}