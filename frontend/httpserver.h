#ifndef HTTPSERVER_H
#define HTTPSERVER_H

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

void HandleCommandLine(int argc, char *argv[]);

int Find_Request_Type(string message);

int Find_URL_Type(string url);

bool Check_Connection(map<string, string> headers);

map<string, string> Receive_Headers(int comm_fd);

bool Handle_Get_Head(string client_request, int comm_fd, bool if_get);

bool Handle_Post(string client_request, int comm_fd);

void Handle_Load_Balancer(int comm_fd);

void Handle_Switchoff(int comm_fd);

void Handle_Resart(int comm_fd);

void *Thread_Running(void *arg);

void Create_Frontend_Server();

#endif
