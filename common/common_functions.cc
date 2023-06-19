/*
 * @Author: Yixuan Meng yixuanm@seas.upenn.edu
 * @Date: 2023-05-01 13:20:35
 * @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 * @LastEditTime: 2023-05-08 19:00:17
 * @FilePath: /T03/common/common_functions.cc
 * @Description: this file contains common functions used by both frontend and backend if needed
 */

#include "common_functions.h"

namespace CommonFunctions
{
    using namespace std;

    /**
     * @description: replace all newlines with <br> tags
     * @param {string} &str
     * @return string
     */
    string replaceNewlinesWithBrTags(const string &str)
    {
        string result = str;
        size_t found = result.find('\n');
        while (found != string::npos)
        {
            result.replace(found, 1, "<br>");
            found = result.find('\n', found + 4);
        }
        return result;
    }

    /**
     * @description: replace all <br> tags with newlines
     * @param {string} &encodedUrl
     * @return string
     */
    string urlDecode(const string &encodedUrl)
    {
        string decodedUrl;
        size_t pos = 0;
        while (pos < encodedUrl.length())
        {
            if (encodedUrl[pos] == '%')
            {
                string hex = encodedUrl.substr(pos + 1, 2);
                int decodedChar = stoi(hex, nullptr, 16);
                decodedUrl.push_back(static_cast<char>(decodedChar));
                pos += 3;
            }
            else if (encodedUrl[pos] == '+')
            {
                // replace '+' with ' '
                decodedUrl.push_back(' ');
                pos++;
            }
            else
            {
                decodedUrl.push_back(encodedUrl[pos]);
                pos++;
            }
        }
        decodedUrl = replaceNewlinesWithBrTags(decodedUrl);
        return decodedUrl;
    }

    /**
     * @description: receive message from client
     * @param {int} ClientFD
     * @return string
     */
    string Receive_Message(int ClientFD)
    {
        string message = "";
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
            if ((int)message.length() > 2000)
            {
                throw "Message too long";
            }
        }
        return message.substr(0, message.length() - 1);
    }

    /**
     * @description: send message to client
     * @param {int} ClientFD
     * @param {string} message_str
     * @return {*}
     */
    void Send_Message(int ClientFD, string message_str)
    {
        char message[2000];
        size_t length;
        length = message_str.length();
        strcpy(message, message_str.c_str());
        send(ClientFD, message, length, MSG_NOSIGNAL);
    }

    /**
     * @description:takes a string and convert it to lowercase
     * @param {string} str
     * @return string
     */
    string to_lowercase(string str)
    {
        for (int i = 0; i < static_cast<int>(str.length()); i++)
        {
            str[i] = tolower(str[i]);
        }
        return str;
    }

    /**
     * @description: remove all spaces in a string
     * @param {string} str
     * @return string
     */
    string remove_spaces(string str)
    {
        str.erase(remove(str.begin(), str.end(), ' '), str.end());
        return str;
    }

    /**
     * @description: convert a number to a fixed length of string e.g. 1 -> "001"
     * @param {string} str
     * @return string
     */
    string format_number(int num, int length)
    {
        ostringstream oss;
        oss << setw(length) << setfill('0') << num;
        return oss.str();
    }

    /**
     * @description: split a string by a delimiter
     * @param {string} &str
     * @param {string} &delimiter
     * @return vector<string>
     */
    vector<string> split_by_delimiter(const string &str, const string &delimiter)
    {
        vector<string> tokens;
        size_t pos = 0;
        size_t end = 0;

        while ((end = str.find(delimiter, pos)) != string::npos)
        {
            tokens.push_back(str.substr(pos, end - pos));
            pos = end + delimiter.length();
        }

        tokens.push_back(str.substr(pos));
        return tokens;
    }

    /**
     * @description: get the current timestamp
     * @return string
     */
    string get_timestamp()
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return to_string(ts.tv_sec) + to_string(ts.tv_nsec);
    }

    /**
     * @description: get the current time in the format of "%H:%M:%S %m-%d-%Y"
     * @return string
     */
    string get_current_time()
    {
        auto t = time(nullptr);
        auto tm = *localtime(&t);
        ostringstream oss;
        oss << put_time(&tm, "%H:%M:%S %m-%d-%Y");
        return oss.str();
    }

    /**
     * @description: get the current timestamp with a delimiter
     * @return string
     */
    string get_timestamp_sep()
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return to_string(ts.tv_sec) + "[TIME]" + to_string(ts.tv_nsec);
    }

    /**
     * @description: compare two timestamps
     * @param {string} &time1
     * @param {string} &time2
     * @return bool
     */
    bool compare_modify_time(const string &time1, const string &time2)
    {
        vector<string> times1 = split_by_delimiter(time1, "[TIME]");
        vector<string> times2 = split_by_delimiter(time2, "[TIME]");
        if (stoul(times1[0]) < stoul(times2[0]) || (stoul(times1[0]) == stoul(times2[0]) && stoul(times1[1]) <= stoul(times2[1])))
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * @description: hash a string to a number (for assigning a client to a server)
     * @param {string} &str
     * @param {unsigned int} n
     * @return unsigned int
     */
    unsigned int hash_string(const string &str, unsigned int n)
    {
        unsigned int hash = 5381;
        for (char c : str)
        {
            hash = ((hash << 5) + hash) + c; // DJB2 hash function
        }
        return hash % n;
    }

    /**
     * @description: "separator".join(list), e.g. [1,2,3] -> "separator" -> "1separator2separator3"
     * @param {vector<bool>} &list
     * @param {string} &delimiter
     * @return string
     */
    string join(const vector<bool> &list, const string &delimiter)
    {
        stringstream ss;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (i > 0)
            {
                ss << delimiter;
            }
            ss << static_cast<int>(list[i]);
        }
        return ss.str();
    }

    /**
     * @description:print out contents of a vector
     * @param {vector<int>} vec
     * @return string
     */
    string inspect_a_vector(vector<int> vec)
    {
        string res = "";
        for (int num : vec)
        {
            res += to_string(num) + " ";
        }
        return res;
    }

    /**
     * @description: process the config file, return the server data list containing the address and port number of each server
     * @param {string} config_file
     * @param {int} server_idx
     * @return vector<server_data>
     */
    vector<server_data> process_config_file(string config_file, int server_idx)
    {
        vector<server_data> server_data_list;
        ifstream config_file_stream(config_file);
        // read config file
        string line;
        struct sockaddr_in bind_addr;
        int count = 0;
        // the first address is the forwarding address and the second is the bind address.
        while (getline(config_file_stream, line))
        {
            // if (verbose_output) cout << line << endl; //127.0.0.1:8000,127.0.0.1:5000
            size_t pos_comma = line.find(",");

            string forward_line = line.substr(0, pos_comma).c_str();

            struct sockaddr_in forward_addr;
            bzero(&forward_addr, sizeof(forward_addr));
            forward_addr.sin_family = AF_INET;

            size_t pos_colon = forward_line.find(":");
            int portno = stoi(forward_line.substr(pos_colon + 1, forward_line.length()));
            int result = inet_pton(AF_INET, forward_line.substr(0, pos_colon).c_str(), &forward_addr.sin_addr);
            if (result == -1)
            {
                cout << "error encoutered\n";
                return server_data_list;
            }
            forward_addr.sin_port = htons(portno);

            // server_list.push_back(forward_addr);
            server_data server;
            server.forward_src = forward_addr;
            server.forward_IPaddr = inet_ntoa(forward_addr.sin_addr);
            server.forward_portno = portno;

            if (count == server_idx)
            {
                if (pos_comma != string::npos)
                {

                    string bind_line = line.substr(pos_comma + 1, line.length()).c_str();
                    bzero(&bind_addr, sizeof(bind_addr));
                    bind_addr.sin_family = AF_INET;

                    pos_colon = bind_line.find(":");
                    portno = stoi(bind_line.substr(pos_colon + 1, bind_line.length()));
                    result = inet_pton(AF_INET, bind_line.substr(0, pos_colon).c_str(), &bind_addr.sin_addr);
                    bind_addr.sin_port = htons(portno);
                }
                else
                    bind_addr = forward_addr;

                server.bind_src = bind_addr;
                server.bind_IPaddr = inet_ntoa(bind_addr.sin_addr);
                server.bind_portno = portno;
            }

            server_data_list.push_back(server);
            count++;
        }
        return server_data_list;
    }

    /**
     * @description: for chunk transfer, read len bytes from fd and store in buf
     * @param {int} fd
     * @param {char} *buf
     * @param {int} len
     * @return bool (whether read successfully)
     */
    bool do_read(int fd, char *buf, int len)
    {
        int rcvd = 0;
        while (rcvd < len)
        {
            int n = read(fd, &buf[rcvd], len - rcvd);
            if (n < 0)
            {
                cout << " do read return false" << endl;
                return false;
            }
            rcvd += n;
            // cout << "do read from " << fd << ", recv:" << rcvd << ", expect:" << len << endl;
        }
        return true;
    }

    /**
     * @description: for chunk transfer, write len bytes from buf to fd
     * @param {int} fd
     * @param {char} *buf
     * @param {int} len
     * @return bool (whether write successfully)
     */
    bool do_write(int fd, const char *buf, int len)
    {
        // keep writing until len bytes sent
        int sent = 0;
        while (sent < len)
        {
            int n = write(fd, &buf[sent], len - sent);
            if (n < 0)
                return false;
            sent += n;
        }
        // cout << "do write to " << fd << ", send " << string(buf) << endl;
        return true;
    }
}