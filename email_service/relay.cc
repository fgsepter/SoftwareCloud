#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
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
#include <sys/select.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

using namespace std;
int portno = 25;
bool verbose_output = false;
string mailboxes_dir;
vector<int> client_socket;
int flock_fd;
// vector<string> ordered_states = {"CONN","HELO","MAIL FROM","RCPT TO","DATA","QUIT"};

vector<string> emails;
vector<string> emails_header;
vector<bool> marked_deleted;

#define SERVER_SHUTDOWN_MESSAGE "421 Service not available, closing transmission channel\r\n"

string get_mail_server_name(const string& domain) {
    if (strcmp(domain.c_str(), "localhost")==0) return "localhost";
    u_char answer[NS_PACKETSZ];
    int len = res_query(domain.c_str(), C_IN, T_MX, answer, sizeof(answer));
    if (len < 0) {
        // DNS query failed
        return "";
    }

    ns_msg handle;
    if (ns_initparse(answer, len, &handle) < 0) {
        // Parsing DNS response failed
        return "";
    }

    ns_rr rr;
    int count = ns_msg_count(handle, ns_s_an);
    for (int i = 0; i < count; ++i) {
        if (ns_parserr(&handle, ns_s_an, i, &rr) < 0) {
            // Parsing DNS record failed
            continue;
        }
        if (ns_rr_type(rr) == ns_t_mx) {
            char mxname[NS_MAXDNAME];
            if (ns_name_uncompress(ns_msg_base(handle), ns_msg_end(handle), ns_rr_rdata(rr) + 2, mxname, sizeof(mxname)) < 0) {
                // Uncompressing MX record failed
                continue;
            }
            return mxname;
        }
    }

    // No MX record found
    return "";
}


void signal_handler(int signal_number){
    // Handle the signal (Ctrl+C)
    // char message[2000];
    // sprintf(message, SERVER_SHUTDOWN_MESSAGE);
    // close(client_socket[0]);
    // for (int i = 1; i < client_socket.size(); i++){
    //     send(client_socket[i], message, strlen(message), 0);
    //     close(client_socket[i]);
    // }
    if (verbose_output) cerr << SERVER_SHUTDOWN_MESSAGE;
    exit(1);
}

string extract_from_and_to(string email, string keyword){
    size_t pos = email.find(keyword);
    if (pos == string::npos) return "";

    pos += 4;
    size_t end = email.find("\n", pos);
    if (end == string::npos) return "";
    string to = email.substr(pos, end - pos);

    pos = to.find("<") + 1;  // find the position of "<"
    end = to.find(">");  // find the position of ">"
    to = to.substr(pos-1, end - pos + 2);
        
    // to = email.substr(pos, end - pos);
    return to;
}

bool single_sending(int* sockfd, const string send_content){
    char buffer[2000];
    char message[2000];
    sprintf(message, "%s" ,send_content.c_str());
    send(*sockfd, message, strlen(message), 0);
    
    ssize_t num_bytes = recv(*sockfd, buffer, sizeof buffer, 0);
    buffer[num_bytes] = '\0';

    if (verbose_output){
        cerr << "[" << *sockfd << "] C: " << message;
        cerr << "[" << *sockfd << "] S: " << buffer;
    }

    bool res=0;
    if (num_bytes > 3 && strncmp(buffer, "250", 3) == 0) res = 1;
    memset(buffer, 0, sizeof buffer);
    return res;
}

vector<string> split_email(const string& email) {
    vector<string> result;
    string delimiter = "\r\n";
    size_t start = 0;
    size_t end = email.find(delimiter);

    while (end != string::npos) {
        result.push_back(email.substr(start, end - start));
        start = end + delimiter.length();
        end = email.find(delimiter, start);
    }

    result.push_back(email.substr(start, end));

    return result;
}

bool send_deliver_request(string from, string to, string email, int* sockfd){
    char buffer[2000];
    char message[2000];
    
    // receive the service ready message
    ssize_t num_bytes = recv(*sockfd, buffer, sizeof buffer-1, 0);
    buffer[num_bytes] = '\0';
    if (verbose_output) cerr << "[" << *sockfd << "] S: " << buffer;
    memset(buffer, 0, sizeof buffer);

    single_sending(sockfd,  "HELO test\r\n");
    single_sending(sockfd,  "MAIL FROM:" + from + "\r\n");
    single_sending(sockfd,  "RCPT TO:" + to + "\r\n");

    single_sending(sockfd,  "DATA\r\n");
    vector<string> splitted_email = split_email(email);
    // cout << "length: " << splitted_email.size() << endl;
    for (string sent: splitted_email) {
        string send_content = sent;
        // cout << send_content;
        sprintf(message, "%s\r\n" ,send_content.c_str());
        send(*sockfd, message, strlen(message), 0);
    }
    bool succeed = single_sending(sockfd,  ".\r\n");
    single_sending(sockfd,  "quit\r\n");
    return succeed;
}

void get_connection_and_deliver(){
    // Deliver each email
    // for (string email : emails) {
    for (int i = 0; i < emails.size(); i++){
        string email = emails[i];
        string to = extract_from_and_to(email, "To: ");
        if (strcmp(to.c_str(), "")==0) continue;
        string from = extract_from_and_to(email, "From: ");
        if (strcmp(from.c_str(), "")==0) continue;
        if (verbose_output) cerr << "Email is sent from " << from << " to " << to << endl;

        // extract mail server
        size_t at = to.find("@");
        if (at == string::npos) continue;
        string domain = to.substr(at + 1);
        domain = domain.substr(0, domain.size() - 1);

        // domain = "localhost";
        if (verbose_output) cerr << "domain: " << domain << endl;

        string server_name = get_mail_server_name(domain);
        //debug
        // server_name = "smtp.gmail.com";
        if (verbose_output) cerr << "mail server name: " << server_name << endl;
        
        // Connect to the mail server and deliver the email
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            continue;
        }

        hostent* host = gethostbyname(server_name.c_str());
        if (verbose_output) cout << "host: " << host << endl;
        if (host == NULL) {
            if (verbose_output) cout << "host is null" << endl;
            close(sockfd);
            continue;
        }

        //reuse of address and port
        int setsockopt(int sockfd, int level, int optname,  const void *optval, socklen_t optlen);
	    int opt = 1;
        int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt));

        struct sockaddr_in servaddr;
        bzero(&servaddr, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = *((unsigned long*)host->h_addr);
        servaddr.sin_port = htons(portno);

        // char* ip = inet_ntoa(*(struct in_addr*)host->h_addr_list[0]);
        // inet_pton(AF_INET, ip, &servaddr.sin_addr);

        // // cout << "here1\n";
        // if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        //     cerr << "Failed to connect to server\n";
        // }
        // // cout << "here2\n";

        // bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
        // listen(listen_fd, 110);
        if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            if (verbose_output) cerr << "Error connecting to server" << endl;
            // exit(1);
        }else if(verbose_output) cerr << "connected to the server" << endl;

        bool succeed = send_deliver_request(from, to, email, &sockfd);
        close(sockfd);
        if (succeed) marked_deleted[i] = true;
    }
}

int main(int argc, char *argv[]){
    // Register the signal handler
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    // sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    // sigaction(MY_SIGNAL, &sa, nullptr);

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

    fd_set set;
    
    string line;
    string email;

    while (1){
        int mqueueFd = open((mailboxes_dir + "/" + "mqueue").c_str(), O_RDONLY);
        emails.clear();
        FD_ZERO(&set);
        FD_SET(mqueueFd, &set);
        int res = select(mqueueFd + 1, &set, NULL, NULL, NULL);
        if (FD_ISSET(mqueueFd, &set)) {
            flock_fd = open((mailboxes_dir + "/" + "mqueue").c_str(), O_RDWR, 0664);// O_RDWR | O_TRUNC
            if (flock_fd == -1 || flock(flock_fd, LOCK_EX) == -1){
                if (verbose_output) cerr << "called flock, cannot access file" << endl;
                continue;
            }

            /* Read */
            ifstream iffile(mailboxes_dir + "/" + "mqueue");
            while (getline(iffile, line)) {
                if (line.substr(0, 5) == "From ") {
                    emails.push_back(email);
                    email = "";
                    emails_header.push_back(line + "\n");
                }else email += line + "\r\n";
            }
            emails.push_back(email);
            emails.erase(emails.begin());
            marked_deleted.resize(emails.size(), false);

            iffile.close();

            if (verbose_output) cerr << "Before delivering, number of emails: " << emails.size() << endl;
            // if (emails.size()==0){
            //     sleep(1);
                
            // }
            // emails.pop_back();
            get_connection_and_deliver();

            int num_emails = count(marked_deleted.begin(), marked_deleted.end(), false);
            if (verbose_output) cerr << "After delivering, number of emails: " << num_emails << endl;

            /* Update */
            ofstream offile;
            offile.open(mailboxes_dir + "/" + "mqueue", ios::out);
            if (!offile.is_open() && verbose_output) {
                  cerr << "Could not open file for writing." << endl;
            }

            for (int i = 0; i < emails.size(); i++) {
                if (marked_deleted[i]) continue;
                // cout << "EMAIL" << email << endl;
                offile << emails_header[i];
                offile << emails[i];
            }
            offile.close();

            if (flock(flock_fd, LOCK_UN) == -1){
                if (verbose_output) cerr << "called flock, cannot unlock file" << endl;
            }
            close(flock_fd);

            // exit(0);
            sleep(1);
        }
    }

    return 0;
}