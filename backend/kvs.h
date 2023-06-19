#ifndef KVS_H // Include guard to prevent multiple inclusions
#define KVS_H

#include <string.h> // Include the string header file
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <arpa/inet.h>
#include <fcntl.h>
#include <numeric>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fstream>
#include <sstream>
#include "../common/common_functions.h"

#define SERVER_SHUTDOWN_MESSAGE "-ERR Server shutting down\n"
#define GOODBYE_MESSAGE "+OK Goodbye!\r\n"
#define UNK_COMMAND_MESSAGE "-ERR Unknown command\r\n"
#define GREETING_MESSAGE "+OK Server ready (Author: Yixuan Meng / yixuanm)\r\n"


using namespace std;

// KVS | PASSWORD | CUR_DIR | ROOT | MAILBOX | FILE_XXX | DIR_XXX | MAIL_XXX | ...
// user1 | ...
// ...
class KeyValueStore_obj
{
private:
    unordered_map<string, unordered_map<string, string>> table;

public:
    string log_path;
    string checkpoint_path;
    unsigned int log_count;
    unsigned int version_num;
    
    KeyValueStore_obj(const string &log, const string &checkpoint);
    ~KeyValueStore_obj();

    // void kvs_put(const string &row, const string &col, const string &val);
    bool kvs_put(const string &row, const string &col, const string &val, const string &modify_time);
    string kvs_get(const string &row, const string &col);
    bool kvs_cput(const string &row, const string &col, const string &old_val, const string &new_val, const string &modify_time);
    // void kvs_delete(const string &row, const string &col);
    bool kvs_delete(const string &row, const string &col, const string &modify_time);

    bool kvs_sign_up(const string &user, const string &pwd, const string &modify_time);
    bool kvs_log_out(const string &user, const string &modify_time);

    string kvs_ls(const string &user);
    bool kvs_upload(const string &user, const string &name, const string &content, const string &file_type, const string &modify_time);
    string kvs_download(const string &user, const string &name);
    bool kvs_mkdir(const string &user, const string &name, const string &modify_time);
    bool kvs_mv(const string &user, const string &name, const string &dst, const string &modify_time);
    bool kvs_rm(const string &user, const string &name, const string &modify_time);
    bool kvs_cd(const string &user, const string &dst, const string &modify_time);
    bool kvs_rename(const string &user, const string &old_name, const string &new_name, const string &modify_time);

    string kvs_list_inbox(const string &user);
    string kvs_open_mail(const string &user, const string &mail_id);
    bool kvs_send_mail(const string &sender, const string &recipient, const string &subject, const string &content, const string &modify_time);
    bool kvs_delete_mail(const string &user, const string &mail_id, const string &modify_time);

    string resolve_absolute_path(const string &user, const string &path);
    string get_absolute_path(const string &user);

    void write_log(const string &operation, const string &modify_time);
    void dump_checkpoint();
    void replay_log();
    void load_checkpoint();
    
    void inspect_kvs_store();
    string kvs_get_raw_data();
    void inspect_kvs_user(const string &user);
};

class Directory_file
{
public:
    unsigned int num_entries;
    string parent_dir;
    vector<string> name;
    vector<unsigned int> size;
    vector<string> type;
    vector<bool> is_dir;
    vector<string> id;

    Directory_file(const string &dir_file_str);

    // "parent_dir\nname|size|type|is_dir|id\n..."
    string to_string();
};

class Mailbox_file
{
public:
    unsigned int num_entries;
    vector<string> subject;
    vector<string> sender;
    vector<string> time;
    vector<string> id;

    Mailbox_file(const string &mbox_file_str);

    // "subject|sender|time|id\n..."
    string to_string();
};

namespace KVS_ns
{
    using namespace std;
    void run_backend_server(int portno, int server_idx, bool verbose_output, KeyValueStore_obj *kvs_store, vector<server_data> server_data_list);
    // void add_test_data_to_kvs_store(KeyValueStore_obj *kvs_store);
    void run_backend_master(int portno, int server_idx, bool verbose_output, vector<server_data> server_data_list);
}

#endif