/*
 * @Author: Yixuan Meng yixuanm@seas.upenn.edu
 * @Date: 2023-05-02 14:52:44
 * @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 * @LastEditTime: 2023-05-08 18:47:21
 * @FilePath: /T03/backend/backend_server.cc
 * @Description: this file contains the helper functions for the backend server
 */

#include "kvs.h"
#include "../common/common_functions.h"
#include <cassert> // Include the header file for assert

using namespace std;

bool verbose_output = true;
bool switched_on = true;
bool is_primary = false;

vector<pthread_t> threads;
vector<int> client_socket;

// initiate mutex_mqueue
pthread_mutex_t mutex_mqueue;
vector<pair<string, int>> primary_queue;

// define the structure for the arguments of the client handler
typedef struct
{
    void *fd;
    KeyValueStore_obj *kvs_store;
    vector<server_data> *server_data_list;
    int server_idx;
    bool verbose_output;
} client_handler_args_backend_server;

typedef struct
{
    vector<server_data> *server_data_list;
    int server_idx;
} primaryq_handler;

void sigpipe_handler(int signum)
{
    // handle SIGPIPE signal
    signal(SIGPIPE, SIG_IGN);
}

/**
 * @description: this function sync kvs data with the primary server of the group
 * @param {vector<server_data>} &server_data_list
 * @param {int} server_idx
 * @param {KeyValueStore_obj} *kvs_store
 * @return {*}
 */
void sync_with_primary(vector<server_data> &server_data_list, int server_idx, KeyValueStore_obj *kvs_store)
{
    // find the primary server
    int rep_start = (server_idx - 1) / num_server_in_group * num_server_in_group + 1;
    server_data s = server_data_list[rep_start];
    sockaddr_in server_src = s.forward_src;
    int sfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sfd < 0)
    {
        fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
        exit(1);
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
    server_addr.sin_port = htons(s.forward_portno);

    // connect to the primary server
    int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == 0)
    {
        // send sync request
        bool new_checkpoint = false;
        string time = CommonFunctions::get_timestamp_sep();
        string request = "fromserver[SEP]" + time + "[SEP]" + to_string(server_idx) + "[SEP]sync_request[SEP]" + to_string(kvs_store->version_num) + "[SEP]" + to_string(kvs_store->log_count) + "\r\n";
        send(sfd, request.c_str(), request.size(), 0);

        // receive sync response
        unsigned int checkpoint_len;
        CommonFunctions::do_read(sfd, (char *)&checkpoint_len, sizeof(checkpoint_len));
        checkpoint_len = ntohl(checkpoint_len);
        // if the primary server has a checkpoint
        if (checkpoint_len != UINT_MAX)
        {
            new_checkpoint = true;

            char buf[checkpoint_len + 1];
            CommonFunctions::do_read(sfd, buf, checkpoint_len);
            buf[checkpoint_len] = 0;

            // overwrite local
            ofstream checkpoint_file;
            checkpoint_file.open(kvs_store->checkpoint_path, ios::out | ios::trunc | ios::binary);
            string checkpoint(buf);
            checkpoint_file << checkpoint;
            checkpoint_file.close();

            kvs_store->load_checkpoint();
        }
        // receive log
        unsigned int log_len;
        CommonFunctions::do_read(sfd, (char *)&log_len, sizeof(log_len));
        log_len = ntohl(log_len);
        if (log_len != UINT_MAX)
        {
            char buf[log_len + 1];
            CommonFunctions::do_read(sfd, buf, log_len);
            buf[log_len] = 0;

            // add to local
            ofstream log_file;
            if (new_checkpoint)
            {
                log_file.open(kvs_store->log_path, ios::out | ios::trunc | ios::binary);
            }
            else
            {
                log_file.open(kvs_store->log_path, ios::out | ios::app | ios::binary);
            }
            string log(buf);
            log_file << log;
            log_file.close();

            kvs_store->replay_log();
        }
    }
    send(sfd, "quit\r\n", strlen("quit\r\n"), 0);
    close(sfd);
}

/**
 * @description: this function handles the client request
 * @param {char} *buffer
 * @param {KeyValueStore_obj} *kvs_store
 * @param {int} comm_fd
 * @param {bool} *will_multicast
 * @return {*}
 */
void handle_sync_request(char *buffer, KeyValueStore_obj *kvs_store, int comm_fd, bool *will_multicast)
{
    vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
    bool need_checkpoint = stoul(fields[1]) < kvs_store->version_num;

    ifstream checkpoint_file;
    checkpoint_file.open(kvs_store->checkpoint_path, ios::in | ios::binary);
    if (need_checkpoint && !checkpoint_file.fail())
    {
        stringstream buf;
        buf << checkpoint_file.rdbuf();
        string checkpoint = buf.str();

        unsigned int checkpoint_len = htonl(checkpoint.size());
        CommonFunctions::do_write(comm_fd, (char *)&checkpoint_len, sizeof(checkpoint_len));
        CommonFunctions::do_write(comm_fd, checkpoint.c_str(), ntohl(checkpoint_len));
    }
    else
    {
        unsigned int checkpoint_len = htonl(UINT_MAX);
        CommonFunctions::do_write(comm_fd, (char *)&checkpoint_len, sizeof(checkpoint_len));
    }
    checkpoint_file.close();

    ifstream log_file;
    log_file.open(kvs_store->log_path, ios::in | ios::binary);
    if (need_checkpoint && !log_file.fail())
    {
        stringstream buf;
        buf << log_file.rdbuf();
        string log = buf.str();

        unsigned int log_len = htonl(log.size());
        CommonFunctions::do_write(comm_fd, (char *)&log_len, sizeof(log_len));
        CommonFunctions::do_write(comm_fd, log.c_str(), ntohl(log_len));
    }
    else if (!need_checkpoint && (stoul(fields[2]) < kvs_store->log_count) && !log_file.fail())
    {
        ifstream log_file;
        log_file.open(kvs_store->log_path, ios::in | ios::binary);
        stringstream buf;
        string line;
        for (auto i = 0; i < kvs_store->log_count; i++)
        {
            getline(log_file, line);
            unsigned int data_size = stoul(line);
            char buffer[data_size + 1];
            log_file.read(buffer, data_size);
            buffer[data_size] = 0;

            if (i >= stoul(fields[2]))
            {
                buf << data_size << endl;
                buf << buffer;
            }
        }
        string log = buf.str();

        unsigned int log_len = htonl(log.size());
        CommonFunctions::do_write(comm_fd, (char *)&log_len, sizeof(log_len));
        CommonFunctions::do_write(comm_fd, log.c_str(), ntohl(log_len));
    }
    else
    {
        unsigned int log_len = htonl(UINT_MAX);
        CommonFunctions::do_write(comm_fd, (char *)&log_len, sizeof(log_len));
    }
    log_file.close();

    *will_multicast = false;
}

/**
 * @description: When a user tries to send an email to external email address (gmail/seas), the email is saved to a file mqueue
 * @param {string} &sender
 * @param {string} &recipient
 * @param {string} &subject
 * @param {string} &content
 * @param {bool} verbose_output
 * @return {*}
 */
void save_external_email_to_mqueue(const string &sender, const string &recipient,
                                   const string &subject, const string &content, bool verbose_output)
{
    /*
        the format of saved mail is as follows
    */
    // sender: username@domain, recipient: username@domain, subject: subject, content: content
    // append a string to file mqueue in the following format:
    // From <sender> Thu Feb 16 21:43:05 2023
    // From: Sender Name <sender>
    // To: recipient Name <recipient>
    // Date: Fri, 21 Oct 2016 18:29:11 -0400
    // Subject: subject
    // content

    string newcontent = CommonFunctions::urlDecode(content);
    string header = "From ";
    header += "<" + sender + "> ";
    time_t rawtime;
    struct tm *timeinfo;
    char tmpbuffer[80];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(tmpbuffer, 80, "%a %b %d %T %Y", timeinfo);
    header += tmpbuffer;
    header += "\n";

    string mail_data = "";
    mail_data += "From: Sender Name <" + sender + ">\n";
    mail_data += "To: recipient Name <" + recipient + ">\n";
    mail_data += "Date: Fri, 21 Oct 2016 18:29:11 -0400\n";
    mail_data += "Subject: " + subject + "\n";
    // mail_data += content + "\n";
    mail_data += newcontent + "\n";

    // write to file
    pthread_mutex_lock(&mutex_mqueue);
    ofstream file;
    file.open("../email_service/mqueue", ios::out | ios::app);

    if (verbose_output)
        cerr << header << mail_data;

    file << header << mail_data;
    file.close();
    pthread_mutex_unlock(&mutex_mqueue);
}

/**
 * @description: multicast message within the replication group
 * @param {char} *message
 * @param {vector<server_data>} server_data_list
 * @param {int} server_idx
 * @param {int} except
 * @return {*}
 */
void multicast_primitives(const char *message, vector<server_data> server_data_list, int server_idx, int except)
{
    int rep_start = (server_idx - 1) / num_server_in_group * num_server_in_group + 1;
    if (is_primary)
    { // multicast to each server in the group
        for (int i = server_idx + 1; i < rep_start + num_server_in_group; i++)
        {
            if (i == except)
            {
                continue;
            }
            // create the sockets
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
                CommonFunctions::Send_Message(sfd, string(message));
                string response = CommonFunctions::Receive_Message(sfd);
                if (verbose_output)
                    cerr << "Primary Server (#" << (server_idx) << ") sends \"" << message << "\" to Server #" << i << endl
                         << "Receives \"" << response << "\" from Server #" << i << endl;
                // send quit msg so that the thread ends
                send(sfd, "quit\r\n", strlen("quit\r\n"), 0);
                close(sfd);
            }
            else
            {
                if (verbose_output)
                    cerr << "connect failed, can't reach server #" << i << endl;
            }
        }
    }
    else
    { // find primary and send to it

        // create socket
        server_data s = server_data_list[rep_start];
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
            CommonFunctions::Send_Message(sfd, string(message));
            if (verbose_output)
            {
                string response = CommonFunctions::Receive_Message(sfd);
                cerr << " Server " << (server_idx) << " sends \"" << message << "\" to Primary Server (#" << rep_start << ")" << endl
                     << "Receives \"" << response << "\" from Primary #" << rep_start << endl;
            }
        }
        // send quit msg so that the thread ends
        send(sfd, "quit\r\n", strlen("quit\r\n"), 0);
        close(sfd);
    }
}

/**
 * @description: multicast message within the replication group - for chunk transfer
 * @param {string} &message
 * @param {vector<server_data>} server_data_list
 * @param {int} server_idx
 * @param {int} except
 * @return {*}
 */
void multicast_primitives_upload(string &message, vector<server_data> server_data_list, int server_idx, int except)
{
    vector<string> fields = CommonFunctions::split_by_delimiter(message, "[SEP]");

    // this function is called as follows, so the info are extracted accordingly
    //  string local_command = "uploadfile[SEP]" + fields[1] + "[SEP]" + fields[2] + "[SEP]" + full_file + "[SEP]" + fields[3];
    //  multicast_primitives_upload(local_command + SEPARATOR + time, server_data_list, server_idx, -1);
    string message_to_server = "fromserver[SEP]" + fields[5] + "[SEP]" + to_string(server_idx) + "[SEP]" + "uploadfile[SEP]" + fields[1] + "[SEP]" + fields[2] + "[SEP]" + fields[4];

    // get the number of chunks
    string &full_file = fields[3];
    unsigned int full_size = full_file.size();
    unsigned int num_chunk = full_size / FILE_CHUNK_SIZE + 1;
    unsigned int last_size = full_size % FILE_CHUNK_SIZE;
    message_to_server += SEPARATOR + to_string(num_chunk);

    for (auto i = 0; i < num_chunk - 1; i++)
    {
        message_to_server += SEPARATOR + to_string(FILE_CHUNK_SIZE);
    }
    message_to_server += SEPARATOR + to_string(last_size) + "\r\n";

    int rep_start = (server_idx - 1) / num_server_in_group * num_server_in_group + 1;
    if (is_primary)
    { // multicast to each server in the group
        for (int i = server_idx + 1; i < rep_start + num_server_in_group; i++)
        {
            if (i == except)
            {
                continue;
            }
            // create the sockets
            server_data s = server_data_list[i];
            sockaddr_in server_src = s.forward_src;
            int sfd = socket(PF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;

            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
            server_addr.sin_port = htons(s.forward_portno);

            int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
            // if the connection is successful, send the file chunk by chunk
            if (ret == 0)
            {
                // the first message inform the file size
                CommonFunctions::Send_Message(sfd, message_to_server);

                for (auto j = 0; j < num_chunk - 1; j++)
                {
                    sleep(1);
                    CommonFunctions::do_write(sfd, full_file.substr(j * FILE_CHUNK_SIZE, FILE_CHUNK_SIZE).c_str(), FILE_CHUNK_SIZE);
                }
                sleep(1);
                CommonFunctions::do_write(sfd, full_file.substr((num_chunk - 1) * FILE_CHUNK_SIZE).c_str(), last_size);

                send(sfd, "quit\r\n", strlen("quit\r\n"), 0);
                close(sfd);
            }
            else
            {
                if (verbose_output)
                    cerr << "connect failed, can't reach server #" << i << endl;
            }
        }
    }
    else
    { // find primary and send to it
        // create the sockets
        server_data s = server_data_list[rep_start];
        sockaddr_in server_src = s.forward_src;
        int sfd = socket(PF_INET, SOCK_STREAM, 0);
        struct sockaddr_in server_addr;

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
        server_addr.sin_port = htons(s.forward_portno);

        int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        // if the connection is successful, send the file chunk by chunk
        if (ret == 0)
        {
            // the first message inform the file size
            CommonFunctions::Send_Message(sfd, message_to_server);
            for (auto j = 0; j < num_chunk - 1; j++)
            {
                sleep(1);
                CommonFunctions::do_write(sfd, full_file.substr(j * FILE_CHUNK_SIZE, FILE_CHUNK_SIZE).c_str(), FILE_CHUNK_SIZE);
            }
            sleep(1);
            CommonFunctions::do_write(sfd, full_file.substr((num_chunk - 1) * FILE_CHUNK_SIZE).c_str(), last_size);
        }
        send(sfd, "quit\r\n", strlen("quit\r\n"), 0);
        close(sfd);
    }
}

/**
 * @description:this function takes command from client (e.g. get(r,c), put(r,c,v)...) and handle it
 * @param {char} *buffer
 * @param {KeyValueStore_obj} *kvs_store
 * @param {int} comm_fd
 * @param {char} *command
 * @param {bool} *client_quit
 * @param {bool} *will_multicast
 * @param {string} time
 * @param {bool} is_from_server
 * @param {int} server_idx
 * @param {vector<server_data>} &server_data_list
 * @param {int} sender_server_index
 * @return {*}
 */
void execute_command_in_buffer(char *buffer,
                               KeyValueStore_obj *kvs_store, int comm_fd, char *command,
                               bool *client_quit, bool *will_multicast, string time,
                               bool is_from_server, int server_idx,
                               vector<server_data> &server_data_list, int sender_server_index)
{
    string response;
    char message[1100000];
    bool noout = false;
    if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "status") == 0 ||
        strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "quit") == 0)
        noout = true;
    // the frontend send get r c\r\n (use a unique seperator [SEP] to seperate the command)
    if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "get[sep]", 8) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
            response = "-ERR Illegal Argument of get(r,c)\r\n";
        else
        {
            if (kvs_store->kvs_get(fields[1], fields[2]) == "")
                response = "-ERR key value pair (r,c) does not exist\r\n";
            else
                response = "+OK " + kvs_store->kvs_get(fields[1], fields[2]) + "\r\n";
        }
        sprintf(message, "%s", response.c_str());
        *will_multicast = false;
    }
    // put r c v\r\n
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "put[sep]", 8) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 4)
            response = "-ERR Illegal Argument of put(r,c,v)\r\n";
        else
        {
            kvs_store->kvs_put(fields[1], fields[2], fields[3], time);
            response = "+OK put " + fields[2] + " into kvs_store successfully.\r\n";
            kvs_store->write_log(buffer, time);

            // multicast the message to all the servers in the group to replicate the data
            *will_multicast = true;
        }
        sprintf(message, "%s", response.c_str());
    }
    // CPUT r c v1 v2
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "cput[sep]", 9) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 5)
            response = "-ERR Illegal Argument of cput(r,c, v1, v2)\r\n";
        else
        {
            int succeed = kvs_store->kvs_cput(fields[1], fields[2], fields[3], fields[4], time);
            if (succeed)
            {
                response = "+OK cput " + fields[4] + " into kvs_store successfully.\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR cput " + fields[4] + " into kvs_store not succeed.\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }
    // delete r c
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "delete[sep]", 11) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
            response = "-ERR Illegal Argument of delete(r,c)\r\n";
        else
        {
            kvs_store->kvs_delete(fields[1], fields[2], time);
            response = "+OK deleted (" + fields[1] + "," + fields[2] + ") from kvs_store successfully.\r\n";
            kvs_store->write_log(buffer, time);
            *will_multicast = true;
        }
        sprintf(message, "%s", response.c_str());
    }

    /* EMAIL */
    // listemail[SEP]username
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "listemail[sep]", 14) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 2)
            response = "-ERR Illegal Argument of listemail(user)\r\n";
        else
        {
            response = "+OK " + kvs_store->kvs_list_inbox(fields[1]) + "\r\n";
            *will_multicast = true;
        }
        sprintf(message, "%s", response.c_str());
    }

    // putemail[SEP]sender[SEP]receiver[SEP]subject[SEP]content // from frontend
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "putemail[sep]", 13) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        string sender = fields[1];
        string receiver = fields[2];
        string subject = fields[3];
        string content = fields[4];
        if (fields.size() != 5)
            response = "-ERR Illegal Argument of listemail(user)\r\n";

        if (fields[2].find("@seas.upenn.edu") != string::npos || fields[2].find("@localhost") != string::npos)
        { // is external or try to send to thunderbird
            response = "+OK send email to external mailbox\r\n";
            save_external_email_to_mqueue(sender, receiver, subject, content, verbose_output);
        }
        if (kvs_store->kvs_send_mail(sender, receiver, subject, content, time))
        {
            response = "+OK put email into kvs_store successfully.\r\n";
            kvs_store->write_log(buffer, time);
            *will_multicast = true;
        }
        else
            response = "-ERR cannot put email into kvs_store.\r\n";
        sprintf(message, "%s", response.c_str());
    }
    // openemail[SEP]username[SEP]emailID
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "openemail[sep]", 14) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
            response = "-ERR Illegal Argument of openemail(user)\r\n";
        else
        {
            response = "+OK " + kvs_store->kvs_open_mail(fields[1], fields[2]) + "\r\n";
        }
        sprintf(message, "%s", response.c_str());
        *will_multicast = false;
    }
    // deleteemail[SEP]username[SEP]emailID
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "deleteemail[sep]", 16) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
            response = "-ERR Illegal arguments\r\n";
        else
        {
            bool succeed = kvs_store->kvs_delete_mail(fields[1], fields[2], time);
            if (succeed)
            {
                response = "+OK Email deleted successfully\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR Failed to delete email\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }
    /* FILE */
    // listfile[SEP]username
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "listfile[sep]", 13) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 2)
        {
            response = "-ERR Illegal Argument of listfile(user)\r\n";
        }
        else
        {
            response = "+OK " + kvs_store->kvs_ls(fields[1]) + "\r\n";
        }
        sprintf(message, "%s", response.c_str());
        *will_multicast = false;
    }
    // uploadfile[SEP]username[SEP]filename[SEP]type[SEP]num_chunk[SEP]size1[SEP]size2[SEP]size3
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "uploadfile[sep]", 15) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() < 6)
        {
            response = "-ERR Illegal Argument of uploadfile(user)\r\n";
        }
        else
        {
            unsigned int num_chunk = stoul(fields[4]);
            string full_file;
            unsigned int file_size;

            for (auto i = 0; i < num_chunk; i++)
            {
                file_size = stoul(fields[5 + i]);
                char file_buf[file_size + 1];

                CommonFunctions::do_read(comm_fd, file_buf, file_size);
                file_buf[file_size] = 0;
                full_file += string(file_buf);
            }

            bool succeed = kvs_store->kvs_upload(fields[1], fields[2], full_file, fields[3], time);
            if (succeed)
            {
                response = "+OK file uploaded into kvs_store successfully.\r\n";
                string local_command = "uploadfile[SEP]" + fields[1] + "[SEP]" + fields[2] + "[SEP]" + full_file + "[SEP]" + fields[3];
                kvs_store->write_log(local_command.c_str(), time);

                // multicast file to others (chunk transfer)
                local_command += SEPARATOR + time;

                if (!is_from_server)
                {
                    multicast_primitives_upload(local_command, server_data_list, server_idx, -1);
                }

                if (is_from_server && is_primary)
                {
                    multicast_primitives_upload(local_command, server_data_list, server_idx, sender_server_index);
                }
            }
            else
            {
                response = "-ERR cannot upload file into kvs_store.\r\n";
            }
        }
        sprintf(message, "%s", response.c_str());
        *will_multicast = false;
    }
    // downloadfile[SEP]username[SEP]filename
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "downloadfile[sep]", 17) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
        {
            response = "-ERR Illegal Argument of openemail(user)\r\n";
        }
        else
        {
            vector<string> full_file = CommonFunctions::split_by_delimiter(kvs_store->kvs_download(fields[1], fields[2]), "[SEP]");
            unsigned int full_size = full_file[1].size();
            unsigned int num_chunk = full_size / FILE_CHUNK_SIZE + 1;
            unsigned int last_size = full_size % FILE_CHUNK_SIZE;
            response = "+OK " + full_file[0] + SEPARATOR + to_string(num_chunk);
            for (auto i = 0; i < num_chunk - 1; i++)
            {
                response += SEPARATOR + to_string(FILE_CHUNK_SIZE);
            }
            response += SEPARATOR + to_string(last_size) + "\r\n";
            sprintf(message, "%s", response.c_str());
            send(comm_fd, message, strlen(message), 0);
            string().swap(response);

            for (auto i = 0; i < num_chunk - 1; i++)
            {
                CommonFunctions::do_write(comm_fd, full_file[1].substr(i * FILE_CHUNK_SIZE, FILE_CHUNK_SIZE).c_str(), FILE_CHUNK_SIZE);
            }
            CommonFunctions::do_write(comm_fd, full_file[1].substr((num_chunk - 1) * FILE_CHUNK_SIZE).c_str(), last_size);
        }
        sprintf(message, "%s", response.c_str());
        *will_multicast = false;
    }

    /* Dir */
    // makedir[SEP]username[SEP]name
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "makedir[sep]", 12) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
        {
            response = "-ERR Illegal arguments\r\n";
        }
        else
        {
            bool succeed = kvs_store->kvs_mkdir(fields[1], fields[2], time);
            if (succeed)
            {
                response = "+OK New folder created successfully\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR Failed to create new folder\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }

    // move[SEP]username[SEP]name[SEP]dst_dir
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "move[sep]", 9) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 4)
        {
            response = "-ERR Illegal arguments\r\n";
        }
        else
        {
            bool succeed = kvs_store->kvs_mv(fields[1], fields[2], fields[3], time);
            if (succeed)
            {
                response = "+OK File/folder moved successfully\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR Failed to move file/folder\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }
    // remove[SEP]username[SEP]name
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "remove[sep]", 11) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
        {
            response = "-ERR Illegal arguments\r\n";
        }
        else
        {
            bool succeed = kvs_store->kvs_rm(fields[1], fields[2], time);
            if (succeed)
            {
                response = "+OK File/folder removed successfully\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR Failed to remove file/folder\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }
    // changedir[SEP]username[SEP]dst_dir
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "changedir[sep]", 14) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
        {
            response = "-ERR Illegal arguments\r\n";
        }
        else
        {
            bool succeed = kvs_store->kvs_cd(fields[1], fields[2], time);
            if (succeed)
            {
                response = "+OK Directory changed successfully\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR Failed to change directory\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }
    // rename[SEP]username[SEP]old_name[SEP]new_name
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "rename[sep]", 11) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 4)
        {
            response = "-ERR Illegal arguments\r\n";
        }
        else
        {
            bool succeed = kvs_store->kvs_rename(fields[1], fields[2], fields[3], time);
            if (succeed)
            {
                response = "+OK File/folder renamed successfully\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR Failed to rename file/folder\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }
    // register[SEP]username[SEP]password
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "register[sep]", 13) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 3)
        {
            response = "-ERR Illegal arguments\r\n";
        }
        else
        {
            bool succeed = kvs_store->kvs_sign_up(fields[1], fields[2], time);
            if (succeed)
            {
                response = "+OK New account created successfully\r\n";
                kvs_store->write_log(buffer, time);
                *will_multicast = true;
            }
            else
                response = "-ERR Failed to create new account\r\n";
        }
        sprintf(message, "%s", response.c_str());
    }
    else if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "rawdata") == 0)
    {
        sprintf(message, "+OK %s\r\n", kvs_store->kvs_get_raw_data().c_str());
        *will_multicast = false;
    }
    // logout, username
    else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "logout[sep]", 11) == 0)
    {
        vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
        if (fields.size() != 2)
        {
            response = "-ERR Illegal arguments\r\n";
        }
        bool succeed = kvs_store->kvs_log_out(fields[1], time);
        if (succeed)
        {
            response = "+OK logout successfully\r\n";
            kvs_store->write_log(buffer, time);
            *will_multicast = true;
        }
        else
            response = "-ERR Failed to logout\r\n";
        sprintf(message, "%s", response.c_str());
    }
    else if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "switchoff") == 0)
    {
        switched_on = false;
        sprintf(message, "%s", "+OK Server switched off\r\n");
        *will_multicast = false;
        *client_quit = true;
    }
    else if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "status") == 0)
    {
        sprintf(message, "+OK %d\r\n", switched_on);
        *will_multicast = false;
    }

    else if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "quit") == 0)
    {
        sprintf(message, GOODBYE_MESSAGE);
        *client_quit = true;
        *will_multicast = false;
    }
    else
    {
        sprintf(message, UNK_COMMAND_MESSAGE);
        *will_multicast = false;
    }
    send(comm_fd, message, strlen(message), 0);

    if (verbose_output && !noout)
    {
        cerr << "[" << comm_fd << "] C: " << buffer << endl;
        cerr << "[" << comm_fd << "] S: " << message;
    }
}

/**
 * @description: this function is executed by a thread when a new connection is established
 * take care of the communication between the client and the backend server
 * extract command from the buffer and execute it
 * @param {void} *args
 * @return {*}
 */
void *client_handler_backend_server(void *args)
{
    signal(SIGPIPE, sigpipe_handler);
    client_handler_args_backend_server *ch_args = (client_handler_args_backend_server *)args;
    void *fd = ch_args->fd;
    KeyValueStore_obj *kvs_store = ch_args->kvs_store;
    bool verbose_output = ch_args->verbose_output;
    int server_idx = ch_args->server_idx;
    int comm_fd = *(int *)fd;
    vector<server_data> server_data_list = *(vector<server_data> *)ch_args->server_data_list;

    if (switched_on)
    {
        char buffer[1100000];
        char tmpbuf[1100000];
        char message[1100000];
        bool client_quit = false;
        int n;
        char *command;    // find the occurrence of "\r\n"
        char *p = buffer; // keep track of the current position in the buffer
        string response;

        while (1)
        {
            n = read(comm_fd, p, sizeof buffer - strlen(buffer));
            if (n == 0)
                continue;

            while ((command = strstr(buffer, "\r\n")) != NULL)
            {
                *command = '\0';
                command += 2;

                bool is_from_server = false;
                bool will_multicast = false; // whether the command is put-related
                bool sync_request = false;
                string time;
                int sender_server_index = -1;

                // is buffer starts with "FROMSERVER" (the message is forwarded by primary), remove "FROMSERVER" prefix
                if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "fromserver[sep]", 13) == 0)
                {
                    is_from_server = true;
                    string str(buffer);
                    // Find the position of the substring "fromserver[sep]"
                    size_t pos = str.find("fromserver[SEP]");
                    if (pos != string::npos)
                        str.erase(pos, string("fromserver[SEP]").length());

                    // get timestamp (time of forwarding)
                    pos = str.find("[SEP]");
                    string sep = "[SEP]";
                    if (pos != string::npos)
                    {
                        time = str.substr(0, pos);
                        str = str.substr(pos + sep.size());
                    }

                    // get sender server index
                    pos = str.find("[SEP]");
                    if (pos != string::npos)
                    {
                        sender_server_index = stoi(str.substr(0, pos));
                        str = str.substr(pos + sep.size());
                    }

                    // get the command by removing the FROMSREVER prefix
                    char *third_sep = buffer;
                    int sep_count = 0;
                    while (*third_sep && sep_count < 4)
                    {
                        if (*third_sep == ']')
                        {
                            sep_count++;
                        }
                        third_sep++;
                    }
                    size_t len = strlen(third_sep);
                    strcpy(tmpbuf, third_sep);

                    if (strncmp(CommonFunctions::to_lowercase(tmpbuf).c_str(), "sync_request[sep]", 17) == 0)
                        sync_request = true;
                }

                if (!is_from_server)
                    time = CommonFunctions::get_timestamp_sep();

                if (sync_request)
                {
                    handle_sync_request(tmpbuf, kvs_store, comm_fd, &will_multicast);
                }
                else
                {
                    if (is_from_server)
                    {
                        execute_command_in_buffer(tmpbuf, kvs_store, comm_fd, command, &client_quit, &will_multicast, time, is_from_server, server_idx, server_data_list, sender_server_index);
                    }
                    else
                    {
                        execute_command_in_buffer(buffer, kvs_store, comm_fd, command, &client_quit, &will_multicast, time, is_from_server, server_idx, server_data_list, sender_server_index);
                    }
                }

                if (!is_from_server && will_multicast)
                {
                    string message_to_server = buffer;
                    assert(message_to_server == buffer);
                    message_to_server = "fromserver[SEP]" + time + "[SEP]" + to_string(server_idx) + "[SEP]" + message_to_server + "\r\n";
                    /*  if primary: forward to all secondaries (do not include primary)
                        if secondary: send to primary only */
                    multicast_primitives(message_to_server.c_str(), server_data_list, server_idx, -1); // don't need to skip secondary here
                }

                if (is_from_server && is_primary && will_multicast)
                {
                    string message_to_server = tmpbuf;
                    assert(message_to_server == tmpbuf);
                    message_to_server = "fromserver[SEP]" + time + "[SEP]" + to_string(server_idx) + "[SEP]" + message_to_server + "\r\n";
                    /*  the msg is forwarded from a secondary to this primary,
                        so skip the sender and multicast to all other secondaries */
                    // multicast_primitives(message_to_server.c_str(), server_data_list, server_idx, sender_server_index);

                    primary_queue.push_back(make_pair(message_to_server.c_str(), sender_server_index));
                }

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
    }
    else
    {
        char buffer[1000000];
        char message[1000000];
        bool client_quit = true;
        int n;
        char *command;    // find the occurrence of "\r\n"
        char *p = buffer; // keep track of the current position in the buffer
        string response;
        bool noout = false;

        while (1)
        {
            n = read(comm_fd, p, sizeof buffer - strlen(buffer));
            if (n == 0)
                continue;

            while ((command = strstr(buffer, "\r\n")) != NULL)
            {
                *command = '\0';
                command += 2;
                if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "status") == 0 ||
                    strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "quit") == 0)
                    noout = true;

                if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "restart", 7) == 0)
                {
                    if (!is_primary)
                    {
                        sync_with_primary(server_data_list, server_idx, kvs_store);
                    }

                    switched_on = true;
                    sprintf(message, "%s", "+OK Server is switched on\r\n");
                }
                else if (strcmp(CommonFunctions::to_lowercase(buffer).c_str(), "status") == 0)
                {
                    sprintf(message, "+OK %d\r\n", switched_on);
                }
                else
                {
                    sprintf(message, "%s", "-ERR Server is switched off\r\n");
                }
                send(comm_fd, message, strlen(message), 0);

                if (verbose_output && !noout)
                    cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer);
                if (verbose_output)
                    cerr << "[" << comm_fd << "] S: " << message;

                if (client_quit)
                    break;
                int len = strlen(command);
                memmove(buffer, command, len + 1);
                memset(buffer + len, 0, command - buffer);
            }
            p = buffer;
            while (*p != '\0')
                p++;
            // quit
            if (client_quit)
                break;
        }
    }
    close(comm_fd);
    pthread_exit(NULL);
    return NULL;
}

/**
 * @description: periodically send messages to other servers in the group to replicate the data
 * @param {void} *args
 * @return {*}
 */
void *primaryq_func(void *args)
{
    primaryq_handler *ch_args = (primaryq_handler *)args;
    int server_idx = ch_args->server_idx;
    vector<server_data> server_data_list = *(vector<server_data> *)ch_args->server_data_list;

    while (1)
    {
        if (primary_queue.size() != 0)
        {
            auto p = primary_queue.begin();
            multicast_primitives(p->first.c_str(), server_data_list, server_idx, p->second);
            primary_queue.erase(primary_queue.begin());
        }
        sleep(0.5);
    }
    pthread_exit(NULL);
    return NULL;
}

/**
 * @description:this function use a while loop to keep listening to the client, create a new thread to handle request from the client
 * @param {int} portno
 * @param {int} server_idx
 * @param {bool} verbose_output
 * @param {KeyValueStore_obj} *kvs_store
 * @param {vector<server_data>} server_data_list
 * @return {*}
 */
void KVS_ns::run_backend_server(int portno, int server_idx, bool verbose_output,
                                KeyValueStore_obj *kvs_store, vector<server_data> server_data_list)
{
    // if server_idx%num_server_in_group==1, then it is the primary node
    if (server_idx % num_server_in_group == 1)
    {
        is_primary = true;
    }

    // 1. logging + checkpointing: read log file
    // 2. recovery: sync data with primary node
    if (!is_primary)
    {
        sync_with_primary(server_data_list, server_idx, kvs_store);
    }

    // Echo server with stream sockets
    // create a new socket
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
    listen(listen_fd, 2048);
    if (verbose_output)
        cerr << "Running on port " << portno << endl;

    pthread_mutex_init(&mutex_mqueue, 0);

    pthread_t thread;
    threads.push_back(thread);
    primaryq_handler args = {
        .server_data_list = &server_data_list,
        .server_idx = server_idx,
    };
    pthread_create(&thread, NULL, &primaryq_func, &args);

    while (true)
    {
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);
        int comm_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
        if (comm_fd == -1)
            break;

        client_socket.push_back(comm_fd);
        pthread_t thread;
        threads.push_back(thread);

        client_handler_args_backend_server args = {
            .fd = &comm_fd,
            .kvs_store = kvs_store,
            .server_data_list = &server_data_list,
            .server_idx = server_idx,
            .verbose_output = verbose_output,
        };
        pthread_create(&thread, NULL, &client_handler_backend_server, &args);
    }
}
