#include "httpserver.h"
#include "frontend_to_client.h"
#include "utils.h"
#include <set>
#include <mutex>
#include "../common/common_functions.h"

using namespace std;

/* DEFINE GLOBAL VARIABLES HERE */
bool vflag = false;
bool switched_on = true; // alive if true
int bflag = 0;
int server_idx = 0;
string config_file = "../common/frontend_servers.txt";
int port_number = DEFAULT_PORT;
int listen_fd, backend_fd;
int master_sock = socket(AF_INET, SOCK_STREAM, 0);
int loadbalancer_sock = socket(AF_INET, SOCK_STREAM, 0);

map<string, string> cookies;
map<string, int> user_servers; // map from username to backend_sock
struct sockaddr_in masteraddr;
struct sockaddr_in loadbalanceraddr;
vector<server_data> backend_server_data_list;
set<int> persistent_connections;
mutex persistent_connections_mutex;

struct args
{
	int ClientFD;
};

/* file structure */
struct files
{
	string filename;
	vector<string> content;
	string type = "";
};

/**
 * Handle command line input
 * @author Yigao Fang
 */
void HandleCommandLine(int argc, char *argv[])
{
	int c;
	while ((c = getopt(argc, argv, "bv")) != -1)
	{
		switch (c)
		{
		case 'v':
			vflag = true;
			break;
		case 'b':
			bflag = 1;
			break;
		case '?':
			throw 2;
		default:
			abort();
		}
	}

	server_idx = stoi(argv[optind]);
	vector<server_data> server_list = CommonFunctions::process_config_file(config_file, server_idx);
	port_number = server_list[server_idx].bind_portno;
	string config_file = "../common/backend_servers.txt";
	backend_server_data_list = CommonFunctions::process_config_file(config_file, server_idx);
}

/**
 * Return the HTTP request type
 * @author Yigao Fang
 */
int Find_Request_Type(string message)
{
	if (regex_match(message, GET_REG))
		return GET;
	if (regex_match(message, POST_REG))
		return POST;
	if (regex_match(message, HEAD_REG))
		return HEAD;
	if (regex_match(message, LOAD_BALANCER_REG))
		return LOAD_BALANCER;
	if (regex_match(message, SWITCH_OFF_REG))
		return SWITCH_OFF;
	if (regex_match(message, RESTART_REG))
		return RESTART;
	return -1;
}

/**
 * Return the request URL type
 * @author Yigao Fang
 */
int Find_URL_Type(string url)
{
	if (regex_match(url, ROOT_REG))
		return ROOT;
	if (regex_match(url, LOGIN_REG))
		return LOGIN;
	if (regex_match(url, LOGIN_VERIFY_REG))
		return LOGIN_VERIFY;
	if (regex_match(url, REGISTER_REG))
		return REGISTER;
	if (regex_match(url, HOMEPAGE_REG))
		return HOMEPAGE;
	if (regex_match(url, WEBMAIL_REG))
		return WEBMAIL;
	if (regex_match(url, WRITEMAIL_REG))
		return WRITEMAIL;
	if (regex_match(url, OPENMAIL_REG))
		return OPENMAIL;
	if (regex_match(url, REPLYMAIL_REG))
		return REPLYMAIL;
	if (regex_match(url, FORWARDMAIL_REG))
		return FORWARDMAIL;
	if (regex_match(url, DELETEMAIL_REG))
		return DELETEMAIL;
	if (regex_match(url, STORAGE_REG))
		return STORAGE;
	if (regex_match(url, UPLOAD_REG))
		return UPLOAD;
	if (regex_match(url, DOWNLOAD_REG))
		return DOWNLOAD;
	if (regex_match(url, CREATE_FOLDER_REG))
		return CREATE_FOLDER;
	if (regex_match(url, MOVE_REG))
		return MOVE;
	if (regex_match(url, REMOVE_REG))
		return REMOVE;
	if (regex_match(url, RENAME_REG))
		return RENAME;
	if (regex_match(url, CHANGE_DIRECTORY_REG))
		return CHANGE_DIRECTORY;
	if (regex_match(url, ADMIN_REG))
		return ADMIN;
	if (regex_match(url, SWITCH_BACKEND_SERVER_REG))
		return SWITCH_BACKEND_SERVER;
	if (regex_match(url, SWITCH_FRONTEND_SERVER_REG))
		return SWITCH_FRONTEND_SERVER;
	if (regex_match(url, CHANGE_PWD_REG))
		return CHANGE_PWD;
	if (regex_match(url, LOGOUT_REG))
		return LOGOUT;
	return UNKOWN;
}

/**
 * Receive header lines from HTTP request
 * @author Yigao Fang
 */
map<string, string> Receive_Headers(int comm_fd)
{
	map<string, string> headers;
	while (true)
	{
		string header = Receive_Message(comm_fd);
		size_t colon_pos = header.find(':');
		if (colon_pos != string::npos)
		{
			string header_name = header.substr(0, colon_pos);
			string header_value = header.substr(colon_pos + 1);
			headers[header_name] = header_value;
		}
		if (header == "") /* Receive an empty line */
			break;
	}
	return headers;
}

/**
 * Find the cookie and sessionID information from HTTP headers
 * @author Yigao Fang
 */
pair<string, string> Find_Cookie(map<string, string> headers)
{
	string name = "";
	string sessionId = "";
	if (headers.find("Cookie") != headers.end())
	{
		string cookieString = headers["Cookie"];
		size_t nameStart = cookieString.find("name=") + 5;
		size_t nameEnd = cookieString.find(";", nameStart);
		size_t sessionIdStart = cookieString.find("sessionid=") + 10;
		size_t sessionIdEnd = cookieString.length();
		name = cookieString.substr(nameStart, nameEnd - nameStart);
		sessionId = cookieString.substr(sessionIdStart, sessionIdEnd - sessionIdStart);
	}
	return make_pair(name, sessionId);
}

/**
 * Check if the current header contains the "Connection:" header line
 * @author Zhaozheng Shen
 */
bool Check_Connection(map<string, string> headers)
{
	string name = "";
	string sessionId = "";
	if (headers.find("Connection") != headers.end())
	{
		string connection = headers["Connection"];
	}
	return false;
}

/**
 * Used for upload request, return a file structure that contains filename, filetype, filecontent
 * @author Yigao Fang
 */
files Receive_DataBody(int comm_fd)
{
	files rst;
	while (true)
	{
		string data = Receive_Message(comm_fd);
		if (data.length() == 0)
			rst.content = Receive_File_Content(comm_fd);
		if (regex_match(data, DATAEND_REG))
		{
			break;
		}
		regex filename_regex("filename=\"([^\"]+)\"");
		smatch match1;
		if (regex_search(data, match1, filename_regex))
		{
			rst.filename = match1[1];
		}
		regex filetype_regex("Content-Type: (.*)");
		smatch match2;
		if (regex_search(data, match2, filetype_regex))
		{
			rst.type = match2[1];
		}
	}
	return rst;
}

/**
 * Receive parameters for POST request (exclude upload request), return a string
 * that contains parameters in format: "para1=value1&para2=value2&..."
 * @author Zhaozheng Shen
 */
string Receive_Parameters(int comm_fd)
{
	bool if_content = false;
	string parameters = "";
	bool first_para = true;
	string newline = "%0D%0A";
	while (true)
	{
		string data = Receive_Message(comm_fd);
		if (regex_match(data, BOUNDARY_REG))
		{
			if_content = false;
			continue;
		}
		else if (regex_match(data, DATAEND_REG))
		{
			break;
		}
		else if (data == "" && if_content == false)
		{
			if_content = true;
		}
		else if (if_content == false)
		{
			regex filename_regex("name=\"([^\"]+)\"");
			smatch match;
			if (regex_search(data, match, filename_regex))
			{
				if (!first_para)
				{
					if (parameters.substr(parameters.length() - newline.length(), parameters.length()) == newline)
						parameters = parameters.substr(0, parameters.length() - newline.length());
				}
				if (!first_para)
					parameters += "&";
				string para = match[1];
				parameters += para;
				parameters += "=";
				first_para = false;
			}
		}
		else if (data == "")
		{
			parameters += newline;
		}
		else
		{
			parameters += data + newline;
		}
	}
	if (parameters.length() - newline.length())
	{
		if (parameters.substr(parameters.length() - newline.length(), parameters.length()) == newline)
			parameters = parameters.substr(0, parameters.length() - newline.length());
	}
	return parameters;
}

/**
 * Generate the HTML content from file ../UI_interface/{url}.html, and directory
 * return the response HTTP
 * @author Zhaozheng Shen
 */
string Get_Response(string filepath, string version, bool if_get)
{
	ifstream file(filepath);
	string response;
	ostringstream oss;
	string line;
	while (getline(file, line))
	{
		oss << line << endl;
		if (line.find("<style>") != string::npos)
		{
			ifstream style_file("../UI_interface/style.css");
			string style_line;
			while (getline(style_file, style_line))
				oss << style_line << endl;
			style_file.close();
		}
	}

	if (file.is_open())
	{ /* Read file contents */
		response = version + " 200 OK\r\n" + "Content-type: text/html\r\n" + "Content-length: " + to_string(oss.str().length()) + "\r\n" + "\r\n";
		if (if_get)
		{
			response += oss.str();
		}
		file.close();
	}
	else
	{
		Debug_Print("Error reading file " + filepath + "\n");
		response = version + " 404 Not Found\r\n";
	}
	return response;
}

/**
 * Used for handling HTTP GET and HTTP HEAD request
 * @author Yigao Fang
 */
bool Handle_Get_Head(string client_request, int comm_fd, bool if_get)
{
	cout << "GET request: " << client_request << endl;
	map<string, string> headers = Receive_Headers(comm_fd);
	stringstream ss(client_request);
	string buf, url, version, filepath, response_msg;
	ss >> buf >> url >> version;

	bool close_connection = Check_Connection(headers);
	pair<string, string> cookie = Find_Cookie(headers);
	MessageToClient response(url, version, Find_URL_Type(url), vflag, port_number, server_idx);
	string username = cookie.first;
	response.setUser(username);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (username != "")
		sockfd = SetBackendSocket(username);

	int request_type = response.getRequestType();
	if (request_type != ROOT and request_type != LOGIN and request_type != LOGIN_VERIFY and request_type != REGISTER and request_type != UNKOWN and request_type != SWITCH_BACKEND_SERVER and request_type != SWITCH_FRONTEND_SERVER)
	{
		/* Verify the sessionId */
		if (response.compareSessionId(cookie, sockfd) == false)
		{
			response_msg = response.prepareMessage();
			Send_Message(comm_fd, response_msg);
			return close_connection;
		}
	}

	if (!switched_on)
		response.handleSwitchoffRedirect();
	else if (request_type == ROOT)
		response.handleRootResponse();
	else if (request_type == LOGIN)
		response.handleStaticResponse();
	else if (request_type == LOGIN_VERIFY)
		response.handleLoginVerifyResponse(sockfd);
	else if (request_type == REGISTER)
		response.handleRegisterResponse(sockfd);
	else if (request_type == HOMEPAGE)
		response.handleHomePage(sockfd);
	else if (request_type == WEBMAIL)
		response.handleWebMailResponse(sockfd);
	else if (request_type == WRITEMAIL)
		response.handleWriteMailResponse(sockfd);
	else if (request_type == OPENMAIL)
		response.handleOpenMailResponse(sockfd);
	else if (request_type == REPLYMAIL)
		response.handleWriteMailResponse(sockfd);
	else if (request_type == FORWARDMAIL)
		response.handleWriteMailResponse(sockfd);
	else if (request_type == DELETEMAIL)
		response.handleDeleteMailResponse(sockfd);
	else if (request_type == STORAGE)
		response.handleStorageResponse(sockfd);
	else if (request_type == DOWNLOAD)
		response.handleDownloadResponse(sockfd);
	else if (request_type == CREATE_FOLDER)
		response.handleCreateFolderResponse(sockfd);
	else if (request_type == MOVE)
		response.handleMoveResponse(sockfd);
	else if (request_type == REMOVE)
		response.handleRemoveResponse(sockfd);
	else if (request_type == RENAME)
		response.handleRenameResponse(sockfd);
	else if (request_type == CHANGE_DIRECTORY)
		response.handleChangeDirectoryResponse(sockfd);
	else if (request_type == ADMIN)
		response.handleAdminResponse(master_sock, loadbalancer_sock);
	else if (request_type == SWITCH_BACKEND_SERVER)
		response.handleSwitchBackendServer(master_sock, loadbalancer_sock);
	else if (request_type == SWITCH_FRONTEND_SERVER)
		response.handleSwitchFrontendServer(master_sock, loadbalancer_sock, &switched_on);
	else if (request_type == CHANGE_PWD)
		response.handleChangePasswordResponse(sockfd);
	else if (request_type == LOGOUT)
		response.handleLogoutResponse(sockfd);
	else
		response.handleFalureResponse();

	if (request_type == DOWNLOAD)
	{
		response_msg = response.downloadMessage();
		write(comm_fd, response_msg.c_str(), response_msg.length());
		vector<string> file_content = response.getFileContent();
		for (const auto &substring : file_content)
		{
			write(comm_fd, substring.c_str(), substring.length());
		}
	}
	else
	{
		response_msg = response.prepareMessage();
		Send_Message(comm_fd, response_msg);
	}
	return close_connection;
}

/**
 * Used for handling HTTP POST request
 * @author Yigao Fang
 */
bool Handle_Post(string client_request, int comm_fd)
{
	// POST response
	cout << "POST request: " << client_request << endl;
	map<string, string> headers = Receive_Headers(comm_fd);
	string parameters = "";
	files file_info;
	stringstream ss(client_request);
	string buf, url, version, response_msg;
	ss >> buf >> url >> version;

	if (Find_URL_Type(url) == UPLOAD)
		file_info = Receive_DataBody(comm_fd);
	else
		parameters = Receive_Parameters(comm_fd);

	bool close_connection = Check_Connection(headers);
	pair<string, string> cookie = Find_Cookie(headers);
	MessageToClient response(parameters, version, Find_URL_Type(url), vflag, port_number, server_idx);
	int request_type = response.getRequestType();
	string username = cookie.first;
	if (username == "" and (request_type == LOGIN or request_type == REGISTER))
	{
		regex pattern("username=(.*)&password=(.*)");
		smatch match;
		regex_search(parameters, match, pattern);
		username = urlDecode(match[1]);
	}
	response.setUser(username);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (username != "")
		sockfd = SetBackendSocket(username);

	if (request_type != ROOT and request_type != LOGIN and request_type != LOGIN_VERIFY and request_type != REGISTER)
	{
		if (response.compareSessionId(cookie, sockfd) == false)
		{
			response_msg = response.prepareMessage();
			Send_Message(comm_fd, response_msg);
			return close_connection;
		}
	}
	if (!switched_on)
		response.handleSwitchoffRedirect();
	else if (request_type == LOGIN)
		response.handleLoginVerifyResponse(sockfd);
	else if (request_type == REGISTER)
		response.handleRegisterResponse(sockfd);
	else if (request_type == CHANGE_PWD)
		response.handleChangePasswordResponse(sockfd);
	else if (request_type == WRITEMAIL)
		response.handleWriteMailResponse(sockfd);
	else if (request_type == REPLYMAIL)
		response.handleWriteMailResponse(sockfd);
	else if (request_type == FORWARDMAIL)
		response.handleWriteMailResponse(sockfd);
	else if (request_type == DELETEMAIL)
		response.handleDeleteMailResponse(sockfd);
	else if (request_type == CREATE_FOLDER)
		response.handleCreateFolderResponse(sockfd);
	else if (request_type == MOVE)
		response.handleMoveResponse(sockfd);
	else if (request_type == RENAME)
		response.handleRenameResponse(sockfd);
	else if (request_type == CHANGE_DIRECTORY)
		response.handleChangeDirectoryResponse(sockfd);
	else if (request_type == UPLOAD)
		response.handleUploadResponse(sockfd, file_info.filename, file_info.content, file_info.type);
	else
		response.handleFalureResponse();
	response_msg = response.prepareMessage();
	Send_Message(comm_fd, response_msg);
	return close_connection;
}

/**
 * Handle message sent from load balancer. if current server is alive, return the
 * number of connections, otherwise return -1.
 * @author Zhaozheng Shen
 */
void Handle_Load_Balancer(int comm_fd)
{
	if (switched_on)
		Send_Message(comm_fd, "+OK " + to_string(persistent_connections.size()) + "\r\n");
	else
		Send_Message(comm_fd, "+OK -1\r\n");
}

/**
 * Switchoff the current server
 * @author Zhaozheng Shen
 */
void Handle_Switchoff(int comm_fd)
{
	switched_on = false;
	Send_Message(comm_fd, "+OK\r\n");
}

/**
 * Restart the current server
 * @author Zhaozheng Shen
 */
void Handle_Resart(int comm_fd)
{
	switched_on = true;
	Send_Message(comm_fd, "+OK\r\n");
}

/**
 * Thread function for multi-thread implementation, each thread resolves one connection from client
 * @author Yigao Fang, Zhaozheng Shen
 */
void *Thread_Running(void *arg)
{
	int comm_fd = ((struct args *)arg)->ClientFD;
	bool is_first_try = true;
	while (true)
	{
		string client_request = Receive_Message(comm_fd);
		if (client_request.size() <= 1)
			continue;
		int request_type = Find_Request_Type(client_request);
		stringstream ss(client_request);
		string buf, url, version;
		bool is_close_connection = false;
		ss >> buf >> url >> version;
		if (is_first_try && request_type != LOAD_BALANCER && request_type != SWITCH_OFF && request_type != RESTART //  && Find_URL_Type(url) != SWITCH_BACKEND_SERVER &&
		)
		{
			persistent_connections.insert(comm_fd);
		}
		if (request_type == GET)
		{
			is_close_connection = Handle_Get_Head(client_request, comm_fd, true);
		}
		else if (request_type == POST)
		{
			is_close_connection = Handle_Post(client_request, comm_fd);
		}
		else if (request_type == HEAD)
		{
			is_close_connection = Handle_Get_Head(client_request, comm_fd, false);
		}
		else if (request_type == LOAD_BALANCER)
		{
			// message from load balancer
			Handle_Load_Balancer(comm_fd);
			close(comm_fd);
			return NULL;
		}
		else if (request_type == SWITCH_OFF)
		{
			// handle switch off
			Handle_Switchoff(comm_fd);
			close(comm_fd);
			return NULL;
		}
		else if (request_type == RESTART)
		{
			// message from restart
			Handle_Resart(comm_fd);
			close(comm_fd);
			return NULL;
		}
		else
		{
			cerr << "-ERR Unknown command\r\n";
			const std::string RESPONSE_TEMPLATE =
				"HTTP/1.1 404 OK\r\n"
				"Connection: keep-alive\r\n"
				"Content-Length: 13\r\n"
				"\r\n"
				"Unknown command!\n";
			send(comm_fd, RESPONSE_TEMPLATE.c_str(), RESPONSE_TEMPLATE.size(), 0);
		}
		is_first_try = false;
		if (version != "HTTP/1.1" || is_close_connection == true)
		{
			persistent_connections.erase(comm_fd);
			break;
		}
	}
	close(comm_fd);
	return NULL;
}

/**
 * Open a new frontend server and begins to listen client requests from users
 * @author Yigao Fang
 */
void Create_Frontend_Server()
{
	struct sockaddr_in servaddr;
	listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
	{
		fprintf(stderr, "Cannot create socket: %s\n", strerror(errno));
		exit(1);
	}
	int val = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &val, sizeof(val)) < 0)
	{
		fprintf(stderr, "Cannot set socket options: %s\n", strerror(errno));
		exit(1);
	}
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(port_number);

	if (bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		fprintf(stderr, "Fail to bind: %s\n", strerror(errno));
		exit(1);
	}
	socklen_t length = sizeof(servaddr);
	if (getsockname(listen_fd, (struct sockaddr *)&servaddr, &length) < 0)
	{
		fprintf(stderr, "Error getting socket name: %s\n", strerror(errno));
		exit(1);
	}
	if (listen(listen_fd, 100) < 0)
	{
		fprintf(stderr, "Listen failed: %s\n", strerror(errno));
		exit(1);
	}
	Debug_Print("Server started on port number " + to_string(port_number) + "\n");

	memset(&masteraddr, 0, sizeof(masteraddr));
	masteraddr.sin_family = AF_INET;

	masteraddr.sin_port = htons(backend_server_data_list[0].forward_portno);
	masteraddr.sin_addr.s_addr = inet_addr(backend_server_data_list[0].forward_IPaddr);
	socklen_t masteraddrlen = sizeof(masteraddr);
	int ret = connect(master_sock, (struct sockaddr *)&masteraddr, sizeof(masteraddr));
	if (ret != 0 && vflag)
		cout << "Master server is not running" << endl;
	else if (vflag)
		cout << "Master server is running" << endl;

	memset(&loadbalanceraddr, 0, sizeof(loadbalanceraddr));
	loadbalanceraddr.sin_family = AF_INET;

	vector<server_data> frontend_server_list = CommonFunctions::process_config_file("../common/frontend_servers.txt", 0);
	loadbalanceraddr.sin_port = htons(frontend_server_list[0].forward_portno);
	loadbalanceraddr.sin_addr.s_addr = inet_addr(frontend_server_list[0].forward_IPaddr);
	ret = connect(loadbalancer_sock, (struct sockaddr *)&loadbalanceraddr, sizeof(loadbalanceraddr));
	if (ret != 0 && vflag)
		cout << "Load balancer is not running" << endl;
	else if (vflag)
		cout << "Load balancer is running" << endl;

	while (true)
	{
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		int comm_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
		if (comm_fd < 0)
		{
			fprintf(stderr, "Accept failed: %s\n", strerror(errno));
			exit(1);
		}
		Debug_Print("[" + to_string(comm_fd) + "] New connection\r\n");
		pthread_t client_thread;
		struct args *input = (struct args *)malloc(sizeof(struct args));
		input->ClientFD = comm_fd;
		pthread_create(&client_thread, NULL, Thread_Running, (void *)input);
	}
	close(listen_fd);
}

/**
 * Main function defined here
 * @author Yigao Fang
 */
int main(int argc, char *argv[])
{
	HandleCommandLine(argc, argv);
	Create_Frontend_Server();
	return 0;
}
