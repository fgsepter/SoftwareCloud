/*
 * @Author: Yixuan Meng yixuanm@seas.upenn.edu
 * @Date: 2023-04-18 14:17:05
 * @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 * @LastEditTime: 2023-05-02 16:59:43
 * @FilePath: /T03/backend/master_node_main.cc
 * @Description: This file containss the main function for the master node (backend), 
 *              which is in charge of the heartbeat and 
 *              assign backend server to frontend when requested.
 */
#include "kvs.h"
#include "../common/common_functions.h"

using namespace std;

vector<user_data> user_data_list;
vector<server_data> server_data_list;
int server_idx = 0;

/**
 * @description: main function for the master node (backend)
 * @param {int} argc
 * @param {char} *argv
 * @return {*}
 */
int main(int argc, char *argv[]){
    /* parse arguments */
    int opt;
    bool verbose_output = false;
    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
            case 'v': {
                verbose_output = true;
                break;
            }
            default:{
                cerr << "Usage: " << " [-v] config_file server_index" << endl;
                cerr << "Author: Yixuan Meng, " << "SEAS login: yixuanm" << endl;
                exit(1);
            }
        }
    }
    if (optind == argc) {
        fprintf(stderr, "*** Author: Yixuan Meng (yixuanm)\n");
        exit(1);
    }
    string config_file = argv[optind];

    optind ++;
    if (optind == argc) {
        fprintf(stderr, "*** Author: Yixuan Meng (yixuanm)\n");
        exit(1);
    }
    
    server_idx = stoi(argv[optind]);
    server_idx --;

    server_data_list = CommonFunctions::process_config_file(config_file, server_idx);
    server_data cur_server = server_data_list[server_idx];
    
    if (verbose_output){
        cout << "Number of servers: " << server_data_list.size() << endl;
        cout << cur_server.bind_IPaddr << ":" << cur_server.bind_portno << endl;
    }

    KVS_ns::run_backend_master(cur_server.bind_portno, server_idx, verbose_output, server_data_list);
    
    return 0;
}
