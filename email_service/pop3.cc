#include <stdlib.h>
#include <stdio.h>
#include <openssl/md5.h>

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

#include <sstream>
#include <iomanip>

#define GREETING_MESSAGE "+OK POP3 server ready [localhost]\r\n"
#define VALID_USER_MESSAGE "+OK name is a valid mailbox\r\n"
#define VALID_PASSWORD_MESSAGE "+OK maildrop locked and ready\r\n"
#define UIDL_OK_MESSAGE "+OK unique-id listing follows\r\n"
#define RETR_OK_MESSAGE "+OK %d octets\r\n"
#define DELETE_OK_MESSAGE "+OK message deleted\r\n"
#define GOODBYE_MESSAGE "+OK POP3 server signing off\r\n"

// unknow command
#define UNK_USER_MESSAGE "-ERR never heard of mailbox name\r\n"
#define UNK_COMMAND_MESSAGE "-ERR Not supported\r\n"
#define SYNTAX_ERR_MESSAGE "-ERR Syntax error in parameters or arguments\r\n"
#define OUTOF_ORDER_MESSAGE "-ERR Bad sequence of commands\r\n"
#define INVALID_PASSWORD_MESSAGE "-ERR invalid password\r\n"
#define PASS_CANNOT_LOCK_MAILDROP "-ERR unable to lock maildrop\r\n" // for pass command, not sure in which scenario
#define NO_SUCH_MSG_MESSAGE "-ERR no such message\r\n"
#define DELE_ERR_MESSAGE "-ERR some deleted messages not removed\r\n"
// shutdown msg not found
#define SERVER_SHUTDOWN_MESSAGE "-ERR Service not available, closing transmission channel\r\n"

using namespace std;
int portno = 11000; // By default, it should use port 11000
bool verbose_output = false;
vector<pthread_t> threads;
vector<int> client_socket;

string mailboxes_dir;
vector<string> mbox_files;
vector<pthread_mutex_t> mailbox_mutexes;

vector<string> ordered_states = {"AUTHORIZATION","TRANSACTION","UPDATE"};
// Once the client identified itself to the POP3 server
//    and the POP3 server has locked and opened the appropriate maildrop,
//    the POP3 session is now in the TRANSACTION state.
// the client issues the QUIT command and the POP3 session enters the UPDATE state.

// TRANSACTION: STAT, LIST, RETR, DELE, NOOP, RSET
// QUIT: if the client issues the QUIT command from the AUTHORIZATION state, the POP3
//    session terminates but does NOT enter the UPDATE state.

void computeDigest(char *data, int dataLengthBytes, unsigned char *digestBuffer)
{
  /* The digest will be written to digestBuffer, which must be at least MD5_DIGEST_LENGTH bytes long */

  MD5_CTX c;
  MD5_Init(&c);
  MD5_Update(&c, data, dataLengthBytes);
  MD5_Final(digestBuffer, &c);
}

string to_lowercase(string str){
  // transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return tolower(c); });
  for (int i = 0; i < str.length(); i++) {
        str[i] = tolower(str[i]);
  }
  return str;
}

void signal_handler(int signal_number){
    // Handle the signal (Ctrl+C)
    // cerr << "signal_handler" << endl;
    // char message[2000];
    // sprintf(message, SERVER_SHUTDOWN_MESSAGE);
    
    if (signal_number == SIGINT){
        close(client_socket[0]);
        for (int i = 1; i < client_socket.size(); i++){
            // if (client_socket[i] != 0){
            // Write the server shutdown message to the client
            send(client_socket[i], SERVER_SHUTDOWN_MESSAGE, strlen(SERVER_SHUTDOWN_MESSAGE), 0);
            close(client_socket[i]);
            pthread_kill(threads[i-1], 0);
        }
        // close(client_socket[0]);
    }
    cout << endl;
}


// Echo client with stream sockets
void* client_handler(void* fd){
    int comm_fd = *(int*)fd;
    write(comm_fd, GREETING_MESSAGE, strlen(GREETING_MESSAGE));
    if (verbose_output) cerr << "[" << comm_fd << "] S: " << GREETING_MESSAGE;

    if (verbose_output) cerr << "client handler: comm_fd = " << comm_fd << endl;
    int state_ptr = 0; // client connected
    // To authenticate using the USER and PASS command combination, 
        // the client must first issue the USER command.
    bool user_issued = false;

    char buffer[10000];
    char message[5000];
    bool client_quit = false;
    int n;
    char *command; // find the occurrence of "\r\n" 
    char *p = buffer; // keep track of the current position in the buffer

    char user_param[100];
    bool no_send = false;
    vector<string> emails;
    vector<string> emails_header;
    vector<bool> marked_deleted;
    int flock_fd; // for flock

    while (1) {
        // memset(buffer, 0, sizeof buffer);
        // n = recv(comm_fd, buffer, sizeof buffer - 1, 0);
        n = read(comm_fd, p, sizeof buffer - strlen(buffer));

        while ((command = strstr(buffer, "\r\n")) != NULL) {
            no_send = false;
            *command = '\0';
            // move it after \r\n
            command += 2;
            // if (verbose_output) cerr << "buffer: " << buffer << endl;
            /* USER linhphan */
            if (strncmp(to_lowercase(buffer).c_str(), "user ", 5) == 0) {
                // cerr << "USER" << endl;
                // can proceed
                if ( strcmp(ordered_states[state_ptr].c_str(),"AUTHORIZATION")==0){
                    string command_param(buffer+5);
                    if (command_param.length() > 0 && 
                        find(mbox_files.begin(), mbox_files.end(), command_param+".mbox") != mbox_files.end()){
                        if (user_issued) sprintf(message, OUTOF_ORDER_MESSAGE);
                        else{
                            // mbox_files.find(command_param+".mbox") != mbox_files.end()){
                            user_issued = true;
                            sprintf(user_param, "%s", (command_param+".mbox").c_str());
                            if (verbose_output) cerr << "user_param: " << user_param << endl;
                            sprintf(message, VALID_USER_MESSAGE);
                        }
                    }else sprintf(message, UNK_USER_MESSAGE);
                } else sprintf(message, OUTOF_ORDER_MESSAGE); // wrong order of states
            } 
            else if (strcmp(to_lowercase(buffer).c_str(), "user") == 0) {sprintf(message, SYNTAX_ERR_MESSAGE);}
            else if (strcmp(to_lowercase(buffer).c_str(), "pass") == 0) {sprintf(message, SYNTAX_ERR_MESSAGE);}
            else if (strcmp(to_lowercase(buffer).c_str(), "retr") == 0) {sprintf(message, SYNTAX_ERR_MESSAGE);}
            else if (strcmp(to_lowercase(buffer).c_str(), "dele") == 0) {sprintf(message, SYNTAX_ERR_MESSAGE);}

            /* PASS cis505 */
            else if (strncmp(to_lowercase(buffer).c_str(), "pass ", 5) == 0) {
                // can proceed
                if ( strcmp(ordered_states[state_ptr].c_str(),"AUTHORIZATION")==0 && user_issued){
                    string command_param(buffer+5);
                    if (command_param.length() > 0 && 
                        strcmp(command_param.c_str(), "cis505")==0){
                            // get email content from xxx.mbox (user_param)
                            auto it = find(mbox_files.begin(), mbox_files.end(), user_param);
                            int index = distance(mbox_files.begin(), it);
                            if(verbose_output) cerr << "index of mbox file: " << index << endl;
                            
                            flock_fd = open((mailboxes_dir + "/" + user_param).c_str(), O_RDWR | O_APPEND, 0664);
                            if (flock_fd == -1 || flock(flock_fd, LOCK_EX) == -1){
                                sprintf(message, "-ERR File handler error\r\n");
                                send(comm_fd, message, strlen(message), 0);
                                if (verbose_output) cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer);
                                if (verbose_output) cerr << "[" << comm_fd << "] S: " << message;
                                continue;
                            }
                            
                            if (pthread_mutex_trylock(&mailbox_mutexes[index]) == 0) {
                                // open file to read emails and save in vector
                                ifstream file(mailboxes_dir + "/" + user_param);
                                string line;
                                string email;
                                while (getline(file, line)) {
                                    if (line.substr(0, 5) == "From ") {
                                        emails.push_back(email);
                                        // email = line + "\n";
                                        email = "";
                                        emails_header.push_back(line + "\n");
                                        // while (getline(file, line) && line.substr(0, 5) != "From ")
                                        //     email += line + "\n";
                                    }else email += line + "\n";
                                }
                                emails.push_back(email);
                                emails.erase(emails.begin());
                                marked_deleted.resize(emails.size(), false);
                                if (verbose_output) cerr << "user: " << user_param << " length of emails: " << emails.size() << endl;
                                file.close();
                                pthread_mutex_unlock(&mailbox_mutexes[index]);

                                state_ptr++;
                                sprintf(message, VALID_PASSWORD_MESSAGE);
                            }
                            else{
                                user_param[0] = '\0';
                                user_issued = false;
                                sprintf(message, PASS_CANNOT_LOCK_MAILDROP);
                            }

                    }else{
                        user_param[0] = '\0';
                        user_issued = false;
                        sprintf(message, INVALID_PASSWORD_MESSAGE);
                    }
                } else sprintf(message, OUTOF_ORDER_MESSAGE); // wrong order of states
            } 

            /* STAT */
            else if (strcmp(to_lowercase(buffer).c_str(), "stat") == 0){
                // can proceed
                if ( strcmp(ordered_states[state_ptr].c_str(),"TRANSACTION")==0 ){
                    int num_emails = count(marked_deleted.begin(), marked_deleted.end(), false);
                    int total_length = 0;
                    for (int i = 0; i < emails.size(); i++){
                        if (!marked_deleted[i]) total_length += emails[i].length();
                    }
                    string res = "+OK ";
                    res += to_string(num_emails) + " " + to_string(total_length) + "\r\n";
                    sprintf(message, "%s", res.c_str());
                } else sprintf(message, OUTOF_ORDER_MESSAGE); // wrong order of states
            } 

            /* UIDL (with argument) */
            else if (strncmp(to_lowercase(buffer).c_str(), "uidl ", 5) == 0 && 
                strcmp(to_lowercase(buffer).c_str(), "uidl ") != 0) {
                if ( strcmp(ordered_states[state_ptr].c_str(),"TRANSACTION")==0 ){
                    string command_param(buffer+5);
                    int idx = atoi(command_param.c_str());
                    idx -- ;

                    if (idx >= 0 && idx < emails.size() && marked_deleted[idx]==false){
                        unsigned char digest[MD5_DIGEST_LENGTH];
                        computeDigest(const_cast<char*>(emails[idx].c_str()), emails[idx].length(), digest);
                        // if (verbose_output) {for (int i = 0; i < MD5_DIGEST_LENGTH; i++) cerr << hex << (int)digest[i]; }
                        stringstream ss;
                        ss << "+OK " << (idx+1) << " ";
                        for (int i = 0; i < MD5_DIGEST_LENGTH; i++){
                          ss << setfill('0') << setw(2) << std::hex << (int)digest[i];
                        }
                        ss << "\r\n";
                        sprintf(message, "%s", ss.str().c_str());
                    }else sprintf(message, NO_SUCH_MSG_MESSAGE); // index of the msg exceed email vector OR refer to deleted msg

                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }
            /* UIDL (without argument) */
            else if (strcmp(to_lowercase(buffer).c_str(), "uidl") == 0 ||
                strcmp(to_lowercase(buffer).c_str(), "uidl ") == 0){
                
                if ( strcmp(ordered_states[state_ptr].c_str(),"TRANSACTION")==0 ){
                    no_send = true;
                    sprintf(message, UIDL_OK_MESSAGE);
                    send(comm_fd, message, strlen(message), 0);
                    if (verbose_output) {
                        cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer) ;
                        cerr << "[" << comm_fd << "] S: " << message;
                    }
                    for (int idx = 0; idx < emails.size(); idx++){
                        if (marked_deleted[idx]==true) continue;
                        unsigned char digest[MD5_DIGEST_LENGTH];
                        computeDigest(const_cast<char*>(emails[idx].c_str()), emails[idx].length(), digest);
                        stringstream ss;
                        ss << (idx+1) << " ";
                        for (int i = 0; i < MD5_DIGEST_LENGTH; i++){
                          ss << setfill('0') << setw(2) << std::hex << (int)digest[i];
                        }
                        ss << "\r\n";
                        sprintf(message, "%s", ss.str().c_str());
                        send(comm_fd, message, strlen(message), 0);
                        if (verbose_output) cerr << "[" << comm_fd << "] S: " << message;
                    }
                    sprintf(message, ".\r\n"); send(comm_fd, message, strlen(message), 0);
                    if (verbose_output) cerr << "[" << comm_fd << "] S: " << ".\r\n";
                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }

            /* RETR */
            else if (strncmp(to_lowercase(buffer).c_str(), "retr ", 5) == 0) {
                if ( strcmp(ordered_states[state_ptr].c_str(),"TRANSACTION")==0 ){
                    string command_param(buffer+5);
                    int idx = atoi(command_param.c_str());
                    idx -- ;
                    if (command_param.length() > 0 ){
                        if (idx >= 0 && idx < emails.size() && marked_deleted[idx]==false){
                            no_send = true;
                            int length = emails[idx].length();
                            sprintf(message, RETR_OK_MESSAGE, length);      send(comm_fd, message, strlen(message), 0);
                            if (verbose_output) {
                                cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer) ;
                                cerr << "[" << comm_fd << "] S: " << message;
                            }

                            istringstream inputStream(emails[idx]);
                            string line;
                            // int cnt=0;
                            while (getline(inputStream, line)) {
                                // cnt ++;
                                // if (cnt == 1)  continue;
                                sprintf(message, "%s\r\n", line.c_str());
                                send(comm_fd, message, strlen(message), 0);
                                if (verbose_output) cerr << "[" << comm_fd << "] S: " << line << endl;
                            }

                            sprintf(message, ".\r\n"); send(comm_fd, message, strlen(message), 0);
                            if (verbose_output) cerr << "[" << comm_fd << "] S: " << ".\r\n";
                        }else sprintf(message, NO_SUCH_MSG_MESSAGE); // index of the msg exceed email vector OR refer to deleted msg
                    }else sprintf(message, SYNTAX_ERR_MESSAGE); // required parameter not found
                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }

            /* DELE */
            // The POP3 server marks the message as deleted.  Any future reference generates an error.  
            // The POP3 server does not actually delete the message until the POP3 session enters the UPDATE state.
            else if (strncmp(to_lowercase(buffer).c_str(), "dele ", 5) == 0) {
                if ( strcmp(ordered_states[state_ptr].c_str(),"TRANSACTION")==0 ){
                    string command_param(buffer+5);
                    int idx = atoi(command_param.c_str());
                    idx -- ;
                    if (command_param.length() > 0 ){
                        if (idx >= 0 && idx < emails.size() && marked_deleted[idx]==false){
                            marked_deleted[idx] = true;
                            sprintf(message, DELETE_OK_MESSAGE);
                        }else sprintf(message, NO_SUCH_MSG_MESSAGE); // index of the msg exceed email vector OR refer to deleted msg
                    }else sprintf(message, SYNTAX_ERR_MESSAGE); // required parameter not found
                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }

            /* QUIT */
            else if (strcmp(to_lowercase(buffer).c_str(), "quit") == 0) {
                client_quit = true;
                // if issues QUIT from the AUTHORIZATION, the POP3 session terminates but does NOT enter the UPDATE state
                // When the client issues the QUIT command from the TRANSACTION state the POP3 session enters the UPDATE state. 
                if (strcmp(ordered_states[state_ptr].c_str(),"TRANSACTION")==0) {
                    state_ptr++; // enter UPDATE
                    // removes all messages marked as deleted from the maildrop
                    auto it = find(mbox_files.begin(), mbox_files.end(), user_param);
                    int index = distance(mbox_files.begin(), it);
                    if(verbose_output) cerr << "dele: overwriting..." << index << endl;
                    if (pthread_mutex_trylock(&mailbox_mutexes[index]) == 0) {
                        ofstream file;
                        file.open(mailboxes_dir + "/" + user_param, ios::out | ios::trunc);
                        if (!file.is_open() && verbose_output) {
                          cerr << "Could not open file for writing." << endl;
                        }
                        for (int i = 0; i < emails.size(); i++){
                            if (marked_deleted[i]) continue;
                            if (verbose_output) cerr << "Writing Message " << i << endl;
                            file << emails_header[i];
                            file << emails[i];
                        }
                        
                        file.close();
                        pthread_mutex_unlock(&mailbox_mutexes[index]);

                        if (flock(flock_fd, LOCK_UN) == -1){
                            sprintf(message, "-ERR File handler error\r\n");
                            send(comm_fd, message, strlen(message), 0);
                            if (verbose_output) cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer);
                            if (verbose_output) cerr << "[" << comm_fd << "] S: " << message;
                            continue;
                        }
                        close(flock_fd);
                    }else{
                        sprintf(message, DELE_ERR_MESSAGE);
                    }
                    // if not successfully write -> err msg, contiue;
                }
                sprintf(message, GOODBYE_MESSAGE);
            } 

            /* LIST (with argument) */
            else if (strncmp(to_lowercase(buffer).c_str(), "list ", 5)==0 &&
                    strcmp(to_lowercase(buffer).c_str(), "list ")!=0){
                if (strcmp(ordered_states[state_ptr].c_str(), "TRANSACTION")==0){
                    string command_param(buffer+5);
                    int idx = atoi(command_param.c_str());
                    idx--;

                    if (idx >= 0 && idx < emails.size() && marked_deleted[idx]==false){
                        stringstream ss;
                        int length = emails[idx].length();
                        ss << "+OK " << (idx+1) << " " << length << "\r\n";
                        sprintf(message, "%s", ss.str().c_str());
                    }else sprintf(message, NO_SUCH_MSG_MESSAGE); // index of the msg exceed email vector OR refer to deleted msg

                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }
            /* LIST (without argument) */
            else if (strcmp(to_lowercase(buffer).c_str(), "list")==0 || 
                    strcmp(to_lowercase(buffer).c_str(), "list ")==0 ){
                if (strcmp(ordered_states[state_ptr].c_str(), "TRANSACTION")==0){
                    no_send = true;
                    int num_emails = count(marked_deleted.begin(), marked_deleted.end(), false);
                    int total_length = 0;
                    for (int i = 0; i < emails.size(); i++){
                        if (!marked_deleted[i]) total_length += emails[i].length();
                    }
                    stringstream ss;
                    ss << "+OK " << num_emails << " messages" << " (" << total_length << " octets)\r\n";
                    sprintf(message, "%s", ss.str().c_str()); send(comm_fd, message, strlen(message), 0);
                    if (verbose_output) {
                        cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer) ;
                        cerr << "[" << comm_fd << "] S: " << message;
                    }
                    for (int idx = 0; idx < emails.size(); idx++){
                        if (marked_deleted[idx]==true) continue;
                        ss.str("");
                        ss << (idx+1) << " " << emails[idx].length() << "\r\n";
                        sprintf(message, "%s", ss.str().c_str());
                        send(comm_fd, message, strlen(message), 0);
                        if (verbose_output) cerr << "[" << comm_fd << "] S: " << message;
                    }
                    sprintf(message, ".\r\n"); send(comm_fd, message, strlen(message), 0);
                    if (verbose_output) cerr << "[" << comm_fd << "] S: " << ".\r\n";
                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }


            /* RSET */
            else if (strcmp(to_lowercase(buffer).c_str(), "rset") == 0) {
                if (strcmp(ordered_states[state_ptr].c_str(), "TRANSACTION")==0){
                    // unmark deleted
                    fill(marked_deleted.begin(), marked_deleted.end(), false);

                    int num_emails = count(marked_deleted.begin(), marked_deleted.end(), false);
                    int total_length = 0;
                    for (int i = 0; i < emails.size(); i++) if (!marked_deleted[i]) total_length += emails[i].length();

                    stringstream ss;
                    ss << "+OK maildrop has " << num_emails << " messages" << " (" << total_length << " octets)\r\n";
                    sprintf(message, "%s", ss.str().c_str());
                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }
            /* NOOP */
            else if (strcmp(to_lowercase(buffer).c_str(), "noop") == 0) {
                if (strcmp(ordered_states[state_ptr].c_str(), "TRANSACTION")==0){
                    sprintf(message, "+OK\r\n");
                }else sprintf(message, OUTOF_ORDER_MESSAGE); //  may only be given in the TRANSACTION state.
            }
            /* nnknown command */
            else {
                sprintf(message, UNK_COMMAND_MESSAGE);
            }
            if (!no_send) send(comm_fd, message, strlen(message), 0);
            if (verbose_output && !no_send) cerr << "[" << comm_fd << "] C: " << string(buffer, command - buffer) ;
            if (verbose_output && !no_send) cerr << "[" << comm_fd << "] S: " << message;
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



int main(int argc, char *argv[])
{
    // Register the signal handler
    // struct sigaction sa;
    // sa.sa_handler = signal_handler;
    // // sigemptyset(&sa.sa_mask);
    // sa.sa_flags = 0;
    // sigaction(SIGINT, &sa, nullptr);
    // // sigaction(MY_SIGNAL, &sa, nullptr);

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
        // sprintf(message, GREETING_MESSAGE);
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

