#include "kvs.h"
#include "../common/common_functions.h"

#define SERVER_SHUTDOWN_MESSAGE "-ERR Server shutting down\n"
#define GOODBYE_MESSAGE "+OK Goodbye!\r\n"
#define UNK_COMMAND_MESSAGE "-ERR Unknown command\r\n"
#define GREETING_MESSAGE "+OK Server ready\r\n"

using namespace std;

static pthread_mutex_t LOG_MUTEX;

KeyValueStore_obj::KeyValueStore_obj(const string &log, const string &checkpoint)
    : log_path(log), checkpoint_path(checkpoint), log_count(0), version_num(0)
{
    pthread_mutex_init(&LOG_MUTEX, NULL);

    load_checkpoint();
    replay_log();

    if (kvs_sign_up("admin@penncloud", "CIS5050T03", "0[TIME]0"))
    {
        write_log("register[SEP]admin@penncloud[SEP]CIS5050T03", "0[TIME]0");
    }
}

KeyValueStore_obj::~KeyValueStore_obj()
{
    pthread_mutex_destroy(&LOG_MUTEX);
}

// void KeyValueStore_obj::kvs_put(const string &row, const string &col, const string &val)
// {
//     table[row][col] = val;
// }

bool KeyValueStore_obj::kvs_put(const string &row, const string &col, const string &val, const string &modify_time)
{
    if (!table[row].count(col + "_LAST_MODIFY") || CommonFunctions::compare_modify_time(table[row][col + "_LAST_MODIFY"], modify_time))
    {
        table[row][col] = val;
        table[row][col + "_LAST_MODIFY"] = modify_time;
        return true;
    }
    else
    {
        return false;
    }
}

string KeyValueStore_obj::kvs_get(const string &row, const string &col)
{
    if (table.count(row) && table[row].count(col))
    {
        return table[row][col];
    }
    else
    {
        return "";
    }
}

bool KeyValueStore_obj::kvs_cput(const string &row, const string &col, const string &old_val, const string &new_val, const string &modify_time)
{
    if (table.count(row) && table[row].count(col) && table[row][col] == old_val)
    {
        kvs_put(row, col, new_val, modify_time);
        return true;
    }
    else
    {
        return false;
    }
}

// void KeyValueStore_obj::kvs_delete(const string &row, const string &col)
// {
//     if (table.count(row) && table[row].count(col))
//     {
//         table[row].erase(col);
//     }
// }

bool KeyValueStore_obj::kvs_delete(const string &row, const string &col, const string &modify_time)
{
    if (table.count(row) && table[row].count(col) && table[row].count(col + "_LAST_MODIFY") && CommonFunctions::compare_modify_time(table[row][col + "_LAST_MODIFY"], modify_time))
    {
        table[row].erase(col);
        table[row].erase(col + "_LAST_MODIFY");
        return true;
    }
    else
    {
        return false;
    }
}

bool KeyValueStore_obj::kvs_sign_up(const string &user, const string &pwd, const string &modify_time)
{
    if (!table.count(user))
    {
        kvs_put(user, "PASSWORD", pwd, modify_time);
        kvs_put(user, "CUR_DIR", "ROOT", modify_time);
        kvs_put(user, "ROOT", "ROOT\n", modify_time);
        kvs_put(user, "MAILBOX", "", modify_time);
        return true;
    }
    else
    {
        return false;
    }
}

bool KeyValueStore_obj::kvs_log_out(const string &user, const string &modify_time)
{
    if (table.count(user))
    {
        kvs_delete(user, "SID", modify_time);
        kvs_put(user, "CUR_DIR", "ROOT", modify_time);
        return true;
    }
    else
    {
        return false;
    }
}

string KeyValueStore_obj::kvs_ls(const string &user)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    string res = get_absolute_path(user) + SEPARATOR;
    for (auto i = 0; i < cur_dir_file.num_entries; i++)
    {
        string is_dir_str = cur_dir_file.is_dir[i] ? "1" : "0";
        res += cur_dir_file.name[i] + SEPARATOR + is_dir_str + SEPARATOR;
    }

    return res;
}

bool KeyValueStore_obj::kvs_upload(const string &user, const string &name, const string &content, const string &file_type, const string &modify_time)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    for (auto i = 0; i < cur_dir_file.num_entries; i++)
    {
        if (cur_dir_file.name[i] == name)
        {
            return false;
        }
    }

    string file_id = "FILE_" + CommonFunctions::get_timestamp();
    kvs_put(user, file_id, content, modify_time);

    cur_dir_file.num_entries++;
    cur_dir_file.name.emplace_back(name);
    cur_dir_file.size.emplace_back(content.size());
    cur_dir_file.type.emplace_back(file_type);
    cur_dir_file.is_dir.emplace_back(false);
    cur_dir_file.id.emplace_back(file_id);

    kvs_put(user, cur_dir, cur_dir_file.to_string(), modify_time);
    return true;
}

string KeyValueStore_obj::kvs_download(const string &user, const string &name)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    for (auto i = 0; i < cur_dir_file.num_entries; i++)
    {
        if (cur_dir_file.name[i] == name && !cur_dir_file.is_dir[i])
        {
            return cur_dir_file.type[i] + SEPARATOR + kvs_get(user, cur_dir_file.id[i]);
        }
    }

    return "";
}

bool KeyValueStore_obj::kvs_mkdir(const string &user, const string &name, const string &modify_time)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    for (auto i = 0; i < cur_dir_file.num_entries; i++)
    {
        if (cur_dir_file.name[i] == name)
        {
            return false;
        }
    }

    string dir_id = "DIR_" + CommonFunctions::get_timestamp();
    kvs_put(user, dir_id, cur_dir + "\n", modify_time);

    cur_dir_file.num_entries++;
    cur_dir_file.name.emplace_back(name);
    cur_dir_file.size.emplace_back(0);
    cur_dir_file.type.emplace_back("");
    cur_dir_file.is_dir.emplace_back(true);
    cur_dir_file.id.emplace_back(dir_id);

    kvs_put(user, cur_dir, cur_dir_file.to_string(), modify_time);
    return true;
}

bool KeyValueStore_obj::kvs_mv(const string &user, const string &name, const string &dst, const string &modify_time)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    string dst_dir = resolve_absolute_path(user, dst);
    if (dst_dir.size() == 0)
        return false;
    if (cur_dir == dst_dir)
        return true;
    Directory_file dst_dir_file(kvs_get(user, dst_dir));
    for (auto i = 0; i < dst_dir_file.num_entries; i++)
    {
        if (dst_dir_file.name[i] == name)
        {
            return false;
        }
    }

    for (auto i = 0; i < cur_dir_file.num_entries; i++)
    {
        if (cur_dir_file.name[i] == name)
        {
            if (cur_dir_file.is_dir[i])
            {
                Directory_file tar_dir_file(kvs_get(user, cur_dir_file.id[i]));
                tar_dir_file.parent_dir = dst_dir;
                kvs_put(user, cur_dir_file.id[i], tar_dir_file.to_string(), modify_time);
            }

            dst_dir_file.num_entries++;
            dst_dir_file.name.emplace_back(cur_dir_file.name[i]);
            dst_dir_file.size.emplace_back(cur_dir_file.size[i]);
            dst_dir_file.type.emplace_back(cur_dir_file.type[i]);
            dst_dir_file.is_dir.emplace_back(cur_dir_file.is_dir[i]);
            dst_dir_file.id.emplace_back(cur_dir_file.id[i]);
            kvs_put(user, dst_dir, dst_dir_file.to_string(), modify_time);

            cur_dir_file.num_entries--;
            cur_dir_file.name.erase(cur_dir_file.name.begin() + i);
            cur_dir_file.size.erase(cur_dir_file.size.begin() + i);
            cur_dir_file.type.erase(cur_dir_file.type.begin() + i);
            cur_dir_file.is_dir.erase(cur_dir_file.is_dir.begin() + i);
            cur_dir_file.id.erase(cur_dir_file.id.begin() + i);
            kvs_put(user, cur_dir, cur_dir_file.to_string(), modify_time);

            return true;
        }
    }

    return false;
}

bool KeyValueStore_obj::kvs_rm(const string &user, const string &name, const string &modify_time)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    if (name == ".")
    {
        for (auto i = 0; i < cur_dir_file.num_entries; i++)
        {
            kvs_rm(user, cur_dir_file.name[i], modify_time);
        }
        return true;
    }
    else
    {
        for (auto i = 0; i < cur_dir_file.num_entries; i++)
        {
            if (cur_dir_file.name[i] == name)
            {
                if (cur_dir_file.is_dir[i])
                {
                    kvs_put(user, "CUR_DIR", cur_dir_file.id[i], modify_time);
                    kvs_rm(user, ".", modify_time);
                    kvs_put(user, "CUR_DIR", cur_dir, modify_time);
                }

                kvs_delete(user, cur_dir_file.id[i], modify_time);

                cur_dir_file.num_entries--;
                cur_dir_file.name.erase(cur_dir_file.name.begin() + i);
                cur_dir_file.size.erase(cur_dir_file.size.begin() + i);
                cur_dir_file.type.erase(cur_dir_file.type.begin() + i);
                cur_dir_file.is_dir.erase(cur_dir_file.is_dir.begin() + i);
                cur_dir_file.id.erase(cur_dir_file.id.begin() + i);

                kvs_put(user, cur_dir, cur_dir_file.to_string(), modify_time);
                return true;
            }
        }
    }

    return false;
}

bool KeyValueStore_obj::kvs_cd(const string &user, const string &dst, const string &modify_time)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));
    if (dst == "..")
    {
        kvs_put(user, "CUR_DIR", cur_dir_file.parent_dir, modify_time);
        return true;
    }
    else
    {
        for (auto i = 0; i < cur_dir_file.num_entries; i++)
        {
            if (cur_dir_file.name[i] == dst && cur_dir_file.is_dir[i])
            {
                kvs_put(user, "CUR_DIR", cur_dir_file.id[i], modify_time);
                return true;
            }
        }

        return false;
    }
}

bool KeyValueStore_obj::kvs_rename(const string &user, const string &old_name, const string &new_name, const string &modify_time)
{
    if (old_name == new_name)
    {
        return true;
    }

    string cur_dir = kvs_get(user, "CUR_DIR");
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    auto tar_idx = -1;
    for (auto i = 0; i < cur_dir_file.num_entries; i++)
    {
        if (cur_dir_file.name[i] == new_name)
        {
            return false;
        }
        if (cur_dir_file.name[i] == old_name)
        {
            tar_idx = i;
        }
    }
    if (tar_idx == -1)
        return false;

    cur_dir_file.name[tar_idx] = new_name;
    kvs_put(user, cur_dir, cur_dir_file.to_string(), modify_time);
    return true;
}

string KeyValueStore_obj::kvs_list_inbox(const string &user)
{
    Mailbox_file inbox(kvs_get(user, "MAILBOX"));

    string res;
    for (auto i = 0; i < inbox.num_entries; i++)
    {
        res += inbox.sender[i] + SEPARATOR + inbox.subject[i] + SEPARATOR + inbox.time[i] + SEPARATOR + inbox.id[i] + SEPARATOR;
    }

    return res;
}

string KeyValueStore_obj::kvs_open_mail(const string &user, const string &mail_id)
{
    Mailbox_file inbox(kvs_get(user, "MAILBOX"));

    string res;
    for (auto i = 0; i < inbox.num_entries; i++)
    {
        if (inbox.id[i] == mail_id)
        {
            return inbox.subject[i] + SEPARATOR + inbox.sender[i] + SEPARATOR + inbox.time[i] + SEPARATOR + kvs_get(user, inbox.id[i]);
        }
    }

    return "";
}

bool KeyValueStore_obj::kvs_send_mail(const string &sender, const string &recipient, const string &subject, const string &content, const string &modify_time)
{
    if (!table.count(recipient))
        return false;

    Mailbox_file inbox(kvs_get(recipient, "MAILBOX"));

    string cur_time = CommonFunctions::get_current_time();
    // string mail_id = "MAIL_" + CommonFunctions::get_timestamp();
    vector<string> fields = CommonFunctions::split_by_delimiter(modify_time, "[TIME]");
    string mail_id = "MAIL_" + fields[0] + fields[1];
    kvs_put(recipient, mail_id, content, modify_time);

    inbox.num_entries++;
    inbox.subject.emplace_back(subject);
    inbox.sender.emplace_back(sender);
    inbox.time.emplace_back(cur_time);
    inbox.id.emplace_back(mail_id);

    kvs_put(recipient, "MAILBOX", inbox.to_string(), modify_time);
    return true;
}

bool KeyValueStore_obj::kvs_delete_mail(const string &user, const string &mail_id, const string &modify_time)
{
    Mailbox_file inbox(kvs_get(user, "MAILBOX"));

    kvs_delete(user, mail_id, modify_time);

    string res;
    for (auto i = 0; i < inbox.num_entries; i++)
    {
        if (inbox.id[i] == mail_id)
        {
            inbox.num_entries--;
            inbox.subject.erase(inbox.subject.begin() + i);
            inbox.sender.erase(inbox.sender.begin() + i);
            inbox.time.erase(inbox.time.begin() + i);
            inbox.id.erase(inbox.id.begin() + i);

            kvs_put(user, "MAILBOX", inbox.to_string(), modify_time);
            return true;
        }
    }

    return false;
}

string KeyValueStore_obj::resolve_absolute_path(const string &user, const string &path)
{
    vector<string> dirs = CommonFunctions::split_by_delimiter(path, "/");
    string cur_dir = "ROOT";

    for (auto dir : dirs)
    {
        if (dir.size() != 0)
        {
            Directory_file cur_dir_file(kvs_get(user, cur_dir));

            bool dir_found = false;
            for (auto i = 0; i < cur_dir_file.num_entries; i++)
            {
                if (cur_dir_file.name[i] == dir && cur_dir_file.is_dir[i])
                {
                    cur_dir = cur_dir_file.id[i];
                    dir_found = true;
                    break;
                }
            }

            if (!dir_found)
            {
                return "";
            }
        }
    }

    return cur_dir;
}

string KeyValueStore_obj::get_absolute_path(const string &user)
{
    string cur_dir = kvs_get(user, "CUR_DIR");
    if (cur_dir == "ROOT")
        return "/";
    Directory_file cur_dir_file(kvs_get(user, cur_dir));

    string res;
    while (cur_dir != "ROOT")
    {
        string last_dir = cur_dir;
        cur_dir = cur_dir_file.parent_dir;
        cur_dir_file = Directory_file(kvs_get(user, cur_dir));
        for (auto i = 0; i < cur_dir_file.num_entries; i++)
        {
            if (cur_dir_file.id[i] == last_dir)
            {
                res = "/" + cur_dir_file.name[i] + res;
                break;
            }
        }
    }

    return res;
}

void KeyValueStore_obj::write_log(const string &operation, const string &modify_time)
{
    pthread_mutex_lock(&LOG_MUTEX);
    if (log_count >= CHECKPOINT_INTERVAL)
    {
        ofstream log_file;
        log_file.open(log_path, ios::out | ios::trunc | ios::binary);
        log_file.close();
        log_count = 0;

        version_num++;
        dump_checkpoint();
    }
    else
    {
        ofstream log_file;
        log_file.open(log_path, ios::out | ios::app | ios::binary);
        string line = operation + SEPARATOR + modify_time;
        log_file << line.size() << endl;
        log_file << line;
        log_file.close();

        log_count++;
    }
    pthread_mutex_unlock(&LOG_MUTEX);
}

void KeyValueStore_obj::dump_checkpoint()
{
    ofstream checkpoint_file;
    checkpoint_file.open(checkpoint_path, ios::out | ios::trunc | ios::binary);
    checkpoint_file << version_num << endl;
    for (pair<string, unordered_map<string, string>> row : table)
    {
        checkpoint_file << row.first << endl;
        checkpoint_file << row.second.size() << endl;
        for (pair<string, string> column : row.second)
        {
            checkpoint_file << column.first << endl;
            checkpoint_file << column.second.size() << endl;
            checkpoint_file << column.second;
        }
    }
    checkpoint_file.close();
}

void KeyValueStore_obj::replay_log()
{
    ifstream log_file;
    log_file.open(log_path, ios::in | ios::binary);
    if (log_file.fail())
        return;

    string line;
    while (getline(log_file, line))
    {
        unsigned int data_size = stoul(line);
        char buffer[data_size + 1];
        log_file.read(buffer, data_size);
        buffer[data_size] = 0;

        if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "put[sep]", 8) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_put(fields[1], fields[2], fields[3], fields[4]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "cput[sep]", 9) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_cput(fields[1], fields[2], fields[3], fields[4], fields[5]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "delete[sep]", 11) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_delete(fields[1], fields[2], fields[3]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "putemail[sep]", 13) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_send_mail(fields[1], fields[2], fields[3], fields[4], fields[5]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "deleteemail[sep]", 16) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_delete_mail(fields[1], fields[2], fields[3]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "uploadfile[sep]", 15) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_upload(fields[1], fields[2], fields[3], fields[4], fields[5]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "makedir[sep]", 12) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_mkdir(fields[1], fields[2], fields[3]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "move[sep]", 9) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_mv(fields[1], fields[2], fields[3], fields[4]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "remove[sep]", 11) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_rm(fields[1], fields[2], fields[3]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "changedir[sep]", 14) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_cd(fields[1], fields[2], fields[3]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "rename[sep]", 11) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_rename(fields[1], fields[2], fields[3], fields[4]);
        }
        else if (strncmp(CommonFunctions::to_lowercase(buffer).c_str(), "register[sep]", 13) == 0)
        {
            vector<string> fields = CommonFunctions::split_by_delimiter(buffer, "[SEP]");
            kvs_sign_up(fields[1], fields[2], fields[3]);
        }

        log_count++;
    }
    log_file.close();
}

void KeyValueStore_obj::load_checkpoint()
{
    ifstream checkpoint_file;
    checkpoint_file.open(checkpoint_path, ios::in | ios::binary);
    if (checkpoint_file.fail())
        return;

    string line;
    getline(checkpoint_file, line);
    version_num = stoul(line);

    unordered_map<string, unordered_map<string, string>>().swap(table);
    while (getline(checkpoint_file, line))
    {
        string row = line;

        getline(checkpoint_file, line);
        unsigned int num_col = stoul(line);

        for (auto i = 0; i < num_col; i++)
        {
            getline(checkpoint_file, line);
            string col = line;

            getline(checkpoint_file, line);
            unsigned int data_size = stoul(line);

            char data[data_size + 1];
            checkpoint_file.read(data, data_size);
            data[data_size] = 0;

            table[row][col] = data;
        }
    }
    checkpoint_file.close();
}

void KeyValueStore_obj::inspect_kvs_store()
{
    for (pair<string, unordered_map<string, string>> row : table)
    {
        cout << row.first << ":\n";

        // Iterate over each column in the row
        int counter = 0;
        for (pair<string, string> column : row.second)
        {
            counter++;
            cout << " " << counter << " " << column.first << ": " << column.second << "\n";
        }
    }
}

string KeyValueStore_obj::kvs_get_raw_data()
{
    const string SEP = "[SEP]";
    string result;
    for (auto row : table)
    {
        for (auto col : row.second)
        {
            // if (row.first!="FILE")
            if (strncmp(CommonFunctions::to_lowercase(row.first).c_str(), "file_", 5) != 0)
                result += row.first + SEP + col.first + SEP + col.second + "[KVS]";
            else
                result += row.first + SEP + col.first + SEP + col.second.substr(0, 20) + "...[KVS]";
        }
    }
    // cout << "result: " << result.length() << endl;
    return result;
}

void KeyValueStore_obj::inspect_kvs_user(const string &user)
{
    // Iterate over each column in the row
    int counter = 0;
    for (pair<string, string> column : table[user])
    {
        counter++;
        cout << counter << " " << column.first << ": " << column.second << "\n";
    }
}

Directory_file::Directory_file(const string &dir_file_str)
{
    istringstream iss(dir_file_str);
    string line;
    getline(iss, parent_dir);

    while (getline(iss, line, '|'))
    {
        name.emplace_back(line);

        getline(iss, line, '|');
        size.emplace_back(stoul(line));

        getline(iss, line, '|');
        type.emplace_back(line);

        getline(iss, line, '|');
        is_dir.emplace_back(line != "0");

        getline(iss, line);
        id.emplace_back(line);
    }

    num_entries = name.size();
}

string Directory_file::to_string()
{
    ostringstream oss;
    oss << parent_dir << "\n";
    for (auto i = 0; i < num_entries; i++)
    {
        oss << name[i] << "|" << size[i] << "|" << type[i] << "|" << is_dir[i] << "|" << id[i] << "\n";
    }
    return oss.str();
}

Mailbox_file::Mailbox_file(const string &mbox_file_str)
{
    istringstream iss(mbox_file_str);
    string line;

    while (getline(iss, line, '|'))
    {
        subject.emplace_back(line);

        getline(iss, line, '|');
        sender.emplace_back(line);

        getline(iss, line, '|');
        time.emplace_back(line);

        getline(iss, line);
        id.emplace_back(line);
    }

    num_entries = subject.size();
}

string Mailbox_file::to_string()
{
    ostringstream oss;
    for (auto i = 0; i < num_entries; i++)
    {
        oss << subject[i] << "|" << sender[i] << "|" << time[i] << "|" << id[i] << "\n";
    }
    return oss.str();
}

// void KVS_ns::add_test_data_to_kvs_store(KeyValueStore_obj *kvs_store)
// {
//     kvs_store->kvs_sign_up("dengbw", "dengbw");
//     kvs_store->kvs_sign_up("fgsepter", "fgsepter");
//     kvs_store->kvs_sign_up("yixuanm", "yixuanm");
//     kvs_store->kvs_sign_up("shenzhzh", "shenzhzh");
// }
