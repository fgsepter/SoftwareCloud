/*
 * @Author: Yixuan Meng yixuanm@seas.upenn.edu
 * @Date: 2023-05-01 13:20:35
 * @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 * @LastEditTime: 2023-05-08 18:53:01
 * @FilePath: /T03/backend/backend_main.cc
 * @Description: This file containss the main function for the backend server
 */
// ./main ../common/config.txt 1 -v

#include "kvs.h"
#include "../common/common_functions.h"

using namespace std;

vector<user_data> user_data_list;
vector<server_data> server_data_list;
int server_idx = 0;

/**
 * @description: the main function for the backend server,
 * parse the arguments and run the backend server
 * @param {int} argc
 * @param {char} *argv
 * @return {*}
 */
int main(int argc, char *argv[])
{
    /* parse arguments */
    int opt;
    bool verbose_output = false;
    while ((opt = getopt(argc, argv, "v")) != -1)
    {
        switch (opt)
        {
        case 'v':
        {
            verbose_output = true;
            break;
        }
        default:
        {
            cerr << "Usage: "
                 << " [-v] config_file server_index" << endl;
            cerr << "Author: Yixuan Meng, "
                 << "SEAS login: yixuanm" << endl;
            exit(1);
        }
        }
    }
    if (optind == argc)
    {
        fprintf(stderr, "*** Author: Yixuan Meng (yixuanm)\n");
        exit(1);
    }
    string config_file = argv[optind];

    optind++;
    if (optind == argc)
    {
        fprintf(stderr, "*** Author: Yixuan Meng (yixuanm)\n");
        exit(1);
    }

    server_idx = stoi(argv[optind]);
    // The index of the first server should be 1 in command
    // convert to 0-indexed
    server_idx--;

    server_data_list = CommonFunctions::process_config_file(config_file, server_idx);
    server_data cur_server = server_data_list[server_idx];
    if (verbose_output)
    {
        cout << "Number of backend servers:" << server_data_list.size() << endl;
        cout << cur_server.bind_IPaddr << ":" << cur_server.bind_portno << endl;
    }

    string log = "kvs_log_" + to_string(server_idx);
    string checkpoint = "kvs_checkpoint_" + to_string(server_idx);
    KeyValueStore_obj kvs_store(log, checkpoint);

    if (verbose_output)
        cout << "Running on port " << cur_server.bind_portno << endl;

    KVS_ns::run_backend_server(cur_server.bind_portno, server_idx, verbose_output, &kvs_store, server_data_list);

    return 0;
}
