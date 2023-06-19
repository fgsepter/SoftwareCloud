#ifndef FROUNTED_UTILS_H
#define FROUNTED_UTILS_H

#include <string>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <ctime>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <cerrno>
#include <sstream>
#include <map>
#include <mutex>
#include "../common/common_functions.h"

using namespace std;

#define UNKOWN -1
#define ROOT 1
#define LOGIN 2
#define LOGIN_VERIFY 3
#define HOMEPAGE 4
#define WEBMAIL 5
#define WRITEMAIL 6
#define OPENMAIL 7
#define STORAGE 8
#define UPLOAD 9
#define DOWNLOAD 10
#define REMOVE 11
#define ADMIN 12
#define REGISTER 13
#define LOGOUT 14
#define CHANGE_PWD 15
#define CREATE_FOLDER 16
#define RENAME 17
#define CHANGE_DIRECTORY 18
#define MOVE 19
#define REPLYMAIL 20
#define FORWARDMAIL 21
#define DELETEMAIL 22
#define SWITCH_BACKEND_SERVER 23
#define SWITCH_FRONTEND_SERVER 24

#define MAX_LENGTH 1000000
#define DEFAULT_PORT 8001

#define EMPTY 0
#define GET 1
#define POST 2
#define HEAD 3
#define LOAD_BALANCER 4
#define SWITCH_OFF 5
#define RESTART 6

// Define the url regex here
const regex ROOT_REG("(/)");
const regex LOGIN_REG("(/login)");
const regex LOGIN_VERIFY_REG("(/login)(.*)");
const regex REGISTER_REG("(/register)");
const regex HOMEPAGE_REG("(/homePage)");
const regex WEBMAIL_REG("(/webmail)");
const regex WRITEMAIL_REG("(/composeEmail)");
const regex OPENMAIL_REG("(/openEmail)(.*)");
const regex REPLYMAIL_REG("(/replyEmail)");
const regex FORWARDMAIL_REG("(/forwardEmail)");
const regex DELETEMAIL_REG("(/deleteEmail)(.*)");
const regex STORAGE_REG("(/storage)");
const regex UPLOAD_REG("(/uploadFile)");
const regex DOWNLOAD_REG("(/download)(.*)");
const regex CREATE_FOLDER_REG("(/createFolder)");
const regex MOVE_REG("(/move)");
const regex REMOVE_REG("(/remove)(.*)");
const regex RENAME_REG("(/rename)");
const regex CHANGE_DIRECTORY_REG("(/changeDirectory)(.*)");
const regex ADMIN_REG("(/admin)");
const regex SWITCH_BACKEND_SERVER_REG("(/switchBackendServer)(.*)");
const regex SWITCH_FRONTEND_SERVER_REG("(/switchFrontendServer)(.*)");
const regex LOGOUT_REG("(/logout)");
const regex CHANGE_PWD_REG("(/changePassword)");

// Define the header line indicator here
const regex BOUNDARY_REG("^--.*[^-]{2}$");
const regex DATAEND_REG("^--.*--$");

// Define the request type regex here
const regex GET_REG("(GET )(.*)", regex::icase);
const regex POST_REG("(POST )(.*)", regex::icase);
const regex HEAD_REG("(HEAD )(.*)", regex::icase);
const regex LOAD_BALANCER_REG("LOAD BALANCER", regex::icase);
const regex SWITCH_OFF_REG("switchoff", regex::icase);
const regex RESTART_REG("restart", regex::icase);

extern bool vflag;
extern vector<server_data> backend_server_data_list;
extern map<string, int> user_servers;
extern int master_sock;
extern map<string, string> cookies;
extern set<int> persistent_connections;
extern mutex persistent_connections_mutex;
extern struct sockaddr_in masteraddr;

void Debug_Print(string info);
string replaceNewlinesWithBrTags(const string &str);
string urlDecode(const string &encodedUrl);
vector<string> getServers(string filename);
pair<int, int> SetSocket(int serverNum);
int SetBackendSocket(string username);
vector<string> Receive_File_Content(int ClientFD);
string Receive_Message(int ClientFD);
void Send_Message(int ClientFD, string message_str);
string Contact_Backend(int sfd, string info);
std::string encode(unsigned char const *, unsigned int len);
std::string decode(std::string const &s);
string addDirectoryLink(string path);
std::string base64_encode(unsigned char const *bytes_to_encode, unsigned int in_len);
std::string base64_decode(std::string const &encoded_string);

void print_ascii(string str);

#endif