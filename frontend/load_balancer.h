#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <iostream>
#include <cerrno>
#include <regex>
#include <sstream>
#include <fstream>
#include <map>

using namespace std;

#define MAX_LENGTH 100000

void HandleCommandLine(int argc, char *argv[]);
string Receive_Message(int ClientFD);
void Send_Message(int ClientFD, string message_str);
map<string, string> Receive_Headers(int comm_fd);
void *Thread_Running(void *arg);
vector<int> get_frontend_status();
void *heartbeat_func(void *arg);
void CreateLoadBalancer();

#endif