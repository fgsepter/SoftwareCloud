#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <vector>
#include <signal.h>

#include <unordered_set>
#include <dirent.h>
#include <netdb.h>
#include <fstream>
#include <fcntl.h>
#include <sys/file.h>
#include <algorithm>

#define SERVER_SHUTDOWN_MESSAGE "421 localhost Service not available, closing transmission channel\r\n"
#define GOODBYE_MESSAGE "221 %s Service closing transmission channel\r\n"
// #define UNK_COMMAND_MESSAGE "-ERR Unknown command\r\n"
#define GREETING_MESSAGE "220 localhost Service Ready\r\n"
#define HELO_START_CONNECTION_MESSAGE "250 %s\r\n"
#define OK_MESSAGE "250 OK\r\n"
#define START_DATA_MESSAGE "354 Start mail input; end with <CRLF>.<CRLF>\r\n"

// unknow command
#define UNK_COMMAND_MESSAGE "500 Syntax error, command unrecognized\r\n"
// If the transaction beginning command argument is not acceptable a 501 failure reply must be returned 
// and the receiver-SMTP must stay in the same state.  
// If the HELO command argument is not acceptable a 501 failure reply must be returned 
// and the receiver-SMTP must stay in the same state.
#define SYNTAX_ERR_MESSAGE "501 Syntax error in parameters or arguments\r\n"

// #define PARAM_ERR_MESSAGE "504 Command parameter not implemented\r\n"
// If the commands in a transaction are out of order a 503 failure reply must be returned 
// and the receiver-SMTP must stay in the same state.
#define OUTOF_ORDER_MESSAGE "503 Bad sequence of commands\r\n"

// If the recipient is unknown the receiver-SMTP returns a 550 Failure reply.
#define UNK_RCPT_MESSAGE "550 Requested action not taken: mailbox unavailable\r\n"
#define TRANSACTION_FAIL "554 Transaction failed\r\n"
using namespace std;
int portno = 2500; // By default, it should use port 2500
bool verbose_output = false;
vector<pthread_t> threads;
vector<int> client_socket;
string mailboxes_dir;

vector<string> mbox_files;
vector<string> ordered_states = {"CONN","HELO","MAIL FROM","RCPT TO","DATA","QUIT"};
// int state_ptr; // should be per client
// if NOOP: need state_ptr >=1 (received HELO, connection started)
// if RSET: state_ptr back to 1

// pthread_mutex_t mailbox_mutex;
vector<pthread_mutex_t> mailbox_mutexes;

string to_lowercase(string str){
  // transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return tolower(c); });
  for (int i = 0; i < str.length(); i++) {
        str[i] = tolower(str[i]);
  }
  return str;
}

// Echo client with stream sockets
void* client_handler(void* fd){
    int comm_fd = *(int*)fd;
    write(comm_fd, GREETING_MESSAGE, strlen(GREETING_MESSAGE));
    if (verbose_output) cerr << "[" << comm_fd << "] S: " << GREETING_MESSAGE;

    if (verbose_output) cerr << "client handler: comm_fd = " << comm_fd << endl;
    int state_ptr = 0; // client connected

    char buffer[10000];
    char message[2000];
    bool client_quit = false;
    int n;
    char *command; // find the occurrence of "\r\n" 
    char *p = buffer; // keep track of the current position in the buffer

    char mailfrom_param[100];
    vector<string> recipients_list;
    string mail_data;
    bool receiving_data = false;
    bool no_send = false;
    int flock_fd;


    while (1) {
        // memset(buffer, 0, sizeof buffer);
        // n = recv(comm_fd, buffer, sizeof buffer - 1, 0);
        n = read(comm_fd, p, sizeof buffer - strlen(buffer));
        // if (verbose_output) cerr << "n" << n << endl;

        while ((command = strstr(buffer, "\r\n")) != NULL) {
            no_send = false;
            *command = '\0';
            // move it after \r\n
            command += 2;
            // if (verbose_output) cerr << "buffer: " << buffer << endl;
            /* HELO <domain> */
            if (strncmp(to_lowercase(buffer).c_str(), "helo ", 5) == 0) {
                // can proceed
                if ( strcmp(ordered_states[state_ptr].c_str(),"CONN")==0 || 
                    strcmp(ordered_states[state_ptr].c_str(),"HELO")==0 ){
                        string command_param(buffer+5);
                        if (command_param.length() > 0 ){
                            state_ptr = 1; // point to HELO
                            // sprintf(message, HELO_START_CONNECTION_MESSAGE, buffer + 5);
                            sprintf(message, HELO_START_CONNECTION_MESSAGE, "localhost");
                        }else sprintf(message, SYNTAX_ERR_MESSAGE);
                } 
                // wrong order of states
                else sprintf(message, OUTOF_ORDER_MESSAGE);
            } 
            else if (strcmp(to_lowercase(buffer).c_str(), "helo") == 0) {sprintf(message, SYNTAX_ERR_MESSAGE);}

            /* MAIL FROM:<user@hostname> */
            else if (strncmp(to_lowercase(buffer).c_str(), "mail from:", 10) == 0){
                // can proceed
                if ( strcmp(ordered_states[state_ptr].c_str(),"HELO")==0 || 
                    strcmp(ordered_states[state_ptr].c_str(),"MAIL FROM")==0 ){
                        string command_param(buffer+10);
                        if (command_param.length() > 0 ){
                            state_ptr = 2; // point to MAIL FROM
                            sprintf(message, OK_MESSAGE);
                            sprintf(mailfrom_param, "%s", command_param.c_str());
                            if (verbose_output) cerr << "mailfrom_param: " << mailfrom_param << endl;
                        }else sprintf(message, SYNTAX_ERR_MESSAGE);
                } // wrong order of states
                else sprintf(message, OUTOF_ORDER_MESSAGE);
            }

            /* RCPT TO:<fred@USC-ISIF.ARPA> */
            /* The second step in the procedure is the RCPT command.
            RCPT <SP> TO:<forward-path> <CRLF>
            This command gives a forward-path identifying one recipient.*/
            else if (strncmp(to_lowercase(buffer).c_str(), "rcpt to:", 8) == 0){
                // can proceed
                if ( strcmp(ordered_states[state_ptr].c_str(),"MAIL FROM")==0 || 
                    strcmp(ordered_states[state_ptr].c_str(),"RCPT TO")==0 ){
                        string command_param(buffer+8);
                        if (command_param.length() > 0 ){
                            // extract username and hostname
                            size_t pos = command_param.find('@');
                            if (pos != std::string::npos) {
                                string username = command_param.substr(1, pos-1);
                                string hostname = command_param.substr(pos + 1, command_param.length()-pos-2);
                                if (verbose_output) cerr << "username = " << username << " hostname = " << hostname << endl;
                                // if (mbox_files.find(username+".mbox") == mbox_files.end() ||
                                if (find(mbox_files.begin(), mbox_files.end(), username+".mbox") == mbox_files.end() ||
                                    strcmp(hostname.c_str(), "localhost") != 0) 
                                        // unfound mailbox or not localhost
                                        sprintf(message, UNK_RCPT_MESSAGE);
                                else{
                                    // if (state_ptr == 3) recipients_list.pop_back(); // should allow multiple recipient
                                    state_ptr = 3; // point to RCPT TO
                                    sprintf(message, OK_MESSAGE);
                                    recipients_list.push_back(username+".mbox");
                                }
                            }
                            else sprintf(message, SYNTAX_ERR_MESSAGE); // invalid mail address (no @ in the address)
                        }else sprintf(message, SYNTAX_ERR_MESSAGE);
                } // wrong order of states
                else sprintf(message, OUTOF_ORDER_MESSAGE);
            }

            /* DATA */
            else if (strcmp(to_lowercase(buffer).c_str(), "data") == 0){
                if ( strcmp(ordered_states[state_ptr].c_str(),"RCPT TO")==0 || 
                    strcmp(ordered_states[state_ptr].c_str(),"DATA")==0 ){
                        if (!receiving_data){
                            receiving_data = true;
                            sprintf(message, START_DATA_MESSAGE);
                            state_ptr = 4; // point to DATA 
                        }
                }
                else sprintf(message, OUTOF_ORDER_MESSAGE);
            } /* DATA continued */
            else if (receiving_data && strcmp(to_lowercase(buffer).c_str(), ".") != 0){ // not the end
                mail_data += string(buffer, command - buffer);
                // cout << string(buffer, command - buffer);
                // if (verbose_output) cerr << mail_data << endl;
                sprintf(message, "%s", "\r\n");
                no_send = true;
            }/* DATA continued */
            else if (receiving_data && strcmp(to_lowercase(buffer).c_str(), ".") == 0){
                receiving_data = false;
                // end of reading data, sending mail:
                for (int i = 0; i < recipients_list.size(); i++) {
                    flock_fd = open((mailboxes_dir + "/" + recipients_list[i]).c_str(), O_RDWR | O_APPEND, 0664);
                    
                    if (flock_fd == -1 || flock(flock_fd, LOCK_EX) == -1){
                        sprintf(message, TRANSACTION_FAIL);
                        send(comm_fd, message, strlen(message), 0);
                        if (verbose_output) cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer);
                        if (verbose_output) cerr << "[" << comm_fd << "] S: " << message;
                        continue;
                    }

                    auto it = find(mbox_files.begin(), mbox_files.end(), recipients_list[i]);
                    int index = distance(mbox_files.begin(), it);
                    // pthread_mutex_lock(&mailbox_mutex);
                    pthread_mutex_lock(&mailbox_mutexes[index]);
                    ofstream file;
                    file.open(mailboxes_dir + "/" + recipients_list[i], ios::out | ios::app);
                    if (!file.is_open() && verbose_output) {
                      cerr << "Could not open file for writing." << endl;
                    }
                    string header = "From ";
                    header += string(mailfrom_param) + " ";
                    time_t rawtime;
                    struct tm * timeinfo;
                    char tmpbuffer[80];
                    time(&rawtime);
                    timeinfo = localtime(&rawtime);
                    strftime(tmpbuffer, 80, "%a %b %d %T %Y", timeinfo);
                    header += tmpbuffer;
                    header += "\n";

                    file << header << mail_data;
                    file.close();
                    // pthread_mutex_unlock(&mailbox_mutex);
                    pthread_mutex_unlock(&mailbox_mutexes[index]);

                    if (flock(flock_fd, LOCK_UN) == -1){
                        sprintf(message, TRANSACTION_FAIL);
                        send(comm_fd, message, strlen(message), 0);
                        if (verbose_output) cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer);
                        if (verbose_output) cerr << "[" << comm_fd << "] S: " << message;
                        continue;
                    }
                    close(flock_fd);

                }
                sprintf(message, OK_MESSAGE);
                state_ptr = 1; // point to HELO
            }
            /* QUIT */
            else if (strcmp(to_lowercase(buffer).c_str(), "quit") == 0) {
                sprintf(message, GOODBYE_MESSAGE, "localhost");
                client_quit = true;
            } 

            /* RSET */
            else if (strcmp(to_lowercase(buffer).c_str(), "rset") == 0) {
                // The first command in a session must be the HELO command.
                if ( strcmp(ordered_states[state_ptr].c_str(),"CONN")!=0 ){
                    state_ptr = 1; // reset to HELO state, aborts a mail transaction
                    // reset everything
                    mailfrom_param[0] = '\0';
                    recipients_list.clear();
                    receiving_data = false;
                    mail_data = "";
                    sprintf(message, OK_MESSAGE);
                }
                // haven't start connection
                else sprintf(message, OUTOF_ORDER_MESSAGE);
            }

            /* NOOP */
            else if (strcmp(to_lowercase(buffer).c_str(), "noop") == 0) {
                // The first command in a session must be the HELO command.
                if ( strcmp(ordered_states[state_ptr].c_str(),"CONN")!=0 ){
                    sprintf(message, OK_MESSAGE);
                }
                // haven't start connection
                else sprintf(message, OUTOF_ORDER_MESSAGE);
            }
            else {
                sprintf(message, UNK_COMMAND_MESSAGE);
            }
            if (!no_send) send(comm_fd, message, strlen(message), 0);
            // if (verbose_output && !no_send) cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer) ;
            if (verbose_output) cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer) ;
            // if (verbose_output && !no_send) cerr << "[" << comm_fd << "] S: " << message;
            if (verbose_output) cerr << "[" << comm_fd << "] S: " << message;
            if (client_quit) break;
            // if (verbose_output) cerr << "before memop, buffer: " << buffer << endl;
            // if (verbose_output) cerr << "before memop, p: " << p << endl;

            int len = strlen(command);
            memmove(buffer, command, len + 1);
            // Clear everything before the pointer
            memset(buffer + len, 0, command - buffer);

            // if (verbose_output) cerr << "after memop, buffer: " << buffer << endl;
            // clear before command, move after-command to the front
            // memmove(buffer, command + 2, sizeof buffer);
        }

        p = buffer;
        while (*p != '\0') p++;

        // quit
        if (client_quit) break;
    }


    close(comm_fd);
    // print("C: Connection closed", comm_fd);
    if (verbose_output) cerr << "[" << comm_fd << "] " << "Connection closed\r\n";
    pthread_exit(NULL);
    
    return NULL;
}


void signal_handler(int signal_number){
    // Handle the signal (Ctrl+C)

    for (int i = 0; i < mailbox_mutexes.size(); i++) {
        pthread_mutex_destroy(&mailbox_mutexes[i]);
    }
    close(client_socket[0]);
    // pthread_kill(threads[0], 0);

    for (int i = 1; i < client_socket.size(); i++){
        // if (client_socket[i] != 0){
        // Write the server shutdown message to the client
        send(client_socket[i], SERVER_SHUTDOWN_MESSAGE, strlen(SERVER_SHUTDOWN_MESSAGE), 0);
        close(client_socket[i]);
        // pthread_kill(threads[i-1], SIGINT);
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    // Register the signal handler
    // struct sigaction sa;
    // sa.sa_handler = signal_handler;
    // sa.sa_flags = 0;
    // sigaction(SIGINT, &sa, nullptr);
    // sigaction(MY_SIGNAL, &sa, nullptr);

    signal(SIGINT, signal_handler);

    /* parse arguments */
    int opt;
    while ((opt = getopt(argc, argv, "p:av")) != -1) {
        switch (opt) {
            case 'p':{
                portno = atoi(optarg);
                break;
            }
            case 'v': {
                verbose_output = true;
                break;
            }
            case 'a': {
                cerr << "Full Name: " << "Yixuan Meng," << " SEAS login: yixuanm" << endl;
                exit(1);
            }
            default:{
                cerr << "Usage: " << " [-p <PORTNO>] [-a] [-v] mailboxes_directory" << endl;
                cerr << "Author: Yixuan Meng, " << "SEAS login: yixuanm" << endl;
                exit(1);
            }
        }
    }
    if (optind == argc) {
        cerr << "No mailboes_directory\nUsage: " << argv[0] << " [-p <PORTNO>] [-a] [-v] mailboxes_directory" << endl;
		exit(1);
	}

    mailboxes_dir = argv[optind];
    // if (verbose_output) cerr << mailboxes_dir << endl;

    // listdir
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(mailboxes_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_type == DT_REG) { // Check if it's a regular file
                // if (verbose_output) cerr << ent->d_name << endl;
                mbox_files.push_back(ent->d_name);
            }
        }
        if (verbose_output) cerr << "Find " << mbox_files.size() << " files in the " << mailboxes_dir << " folder" << endl;
        closedir(dir);
    } else {
        if (verbose_output) cerr << "Could not open directory " << mailboxes_dir << endl;
    }

    mailbox_mutexes.resize(mbox_files.size());
    for (int i = 0; i < mbox_files.size(); i++) {
        int result = pthread_mutex_init(&mailbox_mutexes[i], NULL);
        if (result != 0) {
            if (verbose_output) cerr << "Error initializing mutex" << endl;
            // return result;
        }
    }

    // Echo server with stream sockets
    // create a new socket
    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0){
        if (verbose_output) cerr << "can't open socket" << endl;
        exit(1);
    }
    client_socket.push_back(listen_fd);
    

    //reuse of address and port
    // int setsockopt(int sockfd, int level, int optname,  const void *optval, socklen_t optlen);
	opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);

    // get host name?
    // char host[100];
    // int res = getnameinfo((sockaddr *) &servaddr, sizeof(servaddr), host, sizeof(host), nullptr, 0, 0);
    // cerr << "Domain name: " << host << endl;


    // set to port number
    servaddr.sin_port = htons(portno);
    bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    
    // connect to the server
    listen(listen_fd, 110);
    if (verbose_output) cerr << "Running on port " << portno << endl;

    while (true) {
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);
        int comm_fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
        if (comm_fd == -1) break;
        client_socket.push_back(comm_fd);

        if (verbose_output) cerr << "[" << comm_fd << "] " << "New connection\r\n";
        if (verbose_output) cerr << "Connection from " << inet_ntoa(clientaddr.sin_addr) << endl;
        // if (verbose_output) cerr << "comm_fd: " << comm_fd << endl;

        /* send a simple greeting message and then start a new pthread */
        // write(comm_fd, GREETING_MESSAGE, strlen(GREETING_MESSAGE));
        // if (verbose_output) cerr << "[" << comm_fd << "] S: " << GREETING_MESSAGE;
        // char message[2000];       
        // sprintf(message, GREETING_MESSAGE, "localhost");
        // send(comm_fd, message, strlen(message), 0);

        pthread_t thread;
        threads.push_back(thread);
        pthread_create(&thread, NULL, &client_handler, &comm_fd);
    }
    for (int i = 0; i < mailbox_mutexes.size(); i++) {
        pthread_mutex_destroy(&mailbox_mutexes[i]);
    }

    return 0;
}


