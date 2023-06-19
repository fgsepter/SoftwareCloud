/*
 * @Author: Yixuan Meng yixuanm@seas.upenn.edu
 * @Date: 2023-05-02 22:43:07
 * @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 * @LastEditTime: 2023-05-09 13:23:17
 * @FilePath: /T03/common/common_functions.h
 * @Description: this file includes all the common functions used by both the frontend and the backend servers
 */
#ifndef COMMON_FUNCTIONS_H // Include guard to prevent multiple inclusions
#define COMMON_FUNCTIONS_H

#include <string>
#include <cstring>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <unordered_map>
#include <map>
#include <set>
#include <algorithm>
#include <climits>
using namespace std;

struct server_data
{
    char *forward_IPaddr;
    int forward_portno;
    sockaddr_in forward_src;
    char *bind_IPaddr;
    int bind_portno;
    sockaddr_in bind_src;
};

struct user_data
{ // row in kvs_store = name+id
    int uid;
    string name;
};

#define SEPARATOR "[SEP]"
#define CHECKPOINT_INTERVAL 5
#define FILE_CHUNK_SIZE 1000000
#define num_server_in_group 3

namespace CommonFunctions
{
    using namespace std;

    void Send_Message(int ClientFD, string message_str);
    string Receive_Message(int ClientFD);
    string urlDecode(const string &encodedUrl);
    string replaceNewlinesWithBrTags(const string &str);
    string to_lowercase(string str);
    vector<string> split_by_delimiter(const string &str, const string &delimiter);
    string join(const vector<bool> &list, const string &delimiter);
    string get_timestamp();
    string get_current_time();
    string get_timestamp_sep();
    bool compare_modify_time(const string &time1, const string &time2);
    vector<server_data> process_config_file(string config_file, int server_idx);

    // takes a string str as input and returns a modified string with all the blank spaces removed.
    string remove_spaces(string str);

    //  takes an integer num as input and returns a string with a fixed length of six characters, padded with leading zeros if necessary.
    string format_number(int num, int length);

    // hashes a string using the popular DJB2 hash function and then returns the hash value mod n
    unsigned int hash_string(const string &str, unsigned int n);

    string inspect_a_vector(vector<int> vec);

    bool do_read(int fd, char *buf, int len);
    bool do_write(int fd, const char *buf, int len);
}

#endif // end of COMMON_FUNCTIONS_H
