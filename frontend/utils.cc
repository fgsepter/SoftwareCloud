
#include "utils.h"

#include <iostream>

/* Reference: the encode and decode functions are referenced from https://github.com/ReneNyffenegger/cpp-base64/blob/master/base64.cpp */

/*
   base64.cpp and base64.h
   base64 encoding and decoding with C++.
   More information at
	 https://renenyffenegger.ch/notes/development/Base64/Encoding-and-decoding-base-64-with-cpp
   Version: 2.rc.09 (release candidate)
   Copyright (C) 2004-2017, 2020-2022 René Nyffenegger
   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.
   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:
   1. The origin of this source code must not be misrepresented; you must not
	  claim that you wrote the original source code. If you use this source code
	  in a product, an acknowledgment in the product documentation would be
	  appreciated but is not required.
   2. Altered source versions must be plainly marked as such, and must not be
	  misrepresented as being the original source code.
   3. This notice may not be removed or altered from any source distribution.
   René Nyffenegger rene.nyffenegger@adp-gmbh.ch
*/

static const std::string base64_chars =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789+/";

static inline bool is_base64(unsigned char c)
{
	return (isalnum(c) || (c == '+') || (c == '/'));
}

/* Encode function to transfer binary data to string */
std::string encode(unsigned char const *bytes_to_encode, unsigned int in_len)
{
	std::string ret;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	while (in_len--)
	{
		char_array_3[i++] = *(bytes_to_encode++);
		if (i == 3)
		{
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i < 4); i++)
				ret += base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i)
	{
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while ((i++ < 3))
			ret += '=';
	}

	return ret;
}

/* Decode function to transfer string to binary data */
std::string decode(std::string const &encoded_string)
{
	int in_len = encoded_string.size();
	int i = 0;
	int j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	std::string ret;

	while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_]))
	{
		char_array_4[i++] = encoded_string[in_];
		in_++;
		if (i == 4)
		{
			for (i = 0; i < 4; i++)
				char_array_4[i] = base64_chars.find(char_array_4[i]);

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3); i++)
				ret += char_array_3[i];
			i = 0;
		}
	}

	if (i)
	{
		for (j = 0; j < i; j++)
			char_array_4[j] = base64_chars.find(char_array_4[j]);

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

		for (j = 0; (j < i - 1); j++)
			ret += char_array_3[j];
	}

	return ret;
}

/**
 * Function to print debug output
 * @author Yigao Fang
 */
void Debug_Print(string info)
{
	if (vflag)
	{
		cout << info;
	}
}

/**
 * Parse file content from HTTP request data body
 * @author Zhaozheng Shen
 */
vector<string> Receive_File_Content(int ClientFD)
{
	vector<string> data;
	while (true)
	{
		string message = "";
		/*** Receive message and data from client ***/
		char prev[1];
		while (true)
		{
			char buf[1];
			int n = recv(ClientFD, buf, 1, 0);
			if (buf[0] == '\n' and prev[0] == '\r')
			{
				data.push_back(message.substr(0, message.length() - 1) + "\r\n");
				break;
			}
			if (regex_match(message, DATAEND_REG))
				return data;
			message += buf[0];
			prev[0] = buf[0];
			if (n <= 0)
			{
				return data;
			}
			if ((int)message.length() >= 1000000)
			{
				data.push_back(message);
				break;
			}
		}
	}
	return data;
}

/**
 * Communication function to receive a single message from ClientFD ending with \r\n
 * @author Yigao Fang
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
		if ((int)message.length() > MAX_LENGTH)
		{
			// cout << "Message too long!!!!" << endl;
			throw "Message too long";
		}
	}
	// Debug_Print("[" + to_string(ClientFD) + "] C: " + message + "\n");
	return message.substr(0, message.length() - 1);
}

/**
 * Communication function to send a single message to ClientFD ending with \r\n
 * @author Yigao Fang
 */
void Send_Message(int ClientFD, string message_str)
{
	char message[MAX_LENGTH];
	size_t length;
	length = message_str.length();
	strcpy(message, message_str.c_str());
	send(ClientFD, message, length, MSG_NOSIGNAL);
	// Debug_Print("[" + to_string(ClientFD) + "] S: " + message_str + "\n");
}

/**
 * A function block that sends string info to backend server and receive string response
 * @author Zhaozheng Shen
 */
string Contact_Backend(int sfd, string info)
{
	// send request to backend
	const char *forward_msg = info.c_str();
	Send_Message(sfd, forward_msg);
	// receive response from backend
	string buffer = Receive_Message(sfd);
	return buffer;
}

/**
 * Set the socket of current backend server. Return 0 and the sockfd if success, otherwise return -1
 * @author Yigao Fang
 */
pair<int, int> SetSocket(int serverNum)
{
	int backend_sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in backendaddr;
	memset(&backendaddr, 0, sizeof(backendaddr));
	backendaddr.sin_family = AF_INET;
	backendaddr.sin_port = htons(backend_server_data_list[serverNum].forward_portno);
	backendaddr.sin_addr.s_addr = inet_addr(backend_server_data_list[serverNum].forward_IPaddr);
	socklen_t backendaddrlen = sizeof(backendaddr);
	int ret = connect(backend_sock, (struct sockaddr *)&backendaddr, sizeof(backendaddr));
	if (vflag)
		cout << "Connect to " << backend_server_data_list[serverNum].forward_portno << " " << backend_server_data_list[serverNum].forward_IPaddr << " with ret = " << ret << endl;
	if (ret == 0)
	{
		string status = "status\r\n";
		string output = Contact_Backend(backend_sock, status);
		if (output == "+OK 0")
		{
			return make_pair(-1, backend_sock);
		}
	}
	if (ret == -1)
	{
		if (errno == EISCONN)
		{
			fd_set read_fds;
			FD_ZERO(&read_fds);
			FD_SET(backend_sock, &read_fds);
			struct timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 0;
			int selret = select(backend_sock + 1, &read_fds, NULL, NULL, &timeout);
			if (selret == -1)
			{
				perror("select error");
			}
			else if (selret == 0)
			{
				ret = 0;
			}
			else if (FD_ISSET(backend_sock, &read_fds))
			{
				cout << "Connection closed by server, EOF received" << endl;
			}
		}
		else
		{
			perror("connect error");
		}
	}
	return make_pair(ret, backend_sock);
}

/**
 * Set the socket of current backend server until one successful connection
 * @author Yigao Fang
 */
int SetBackendSocket(string username)
{
	int backend_sock = socket(AF_INET, SOCK_STREAM, 0);
	auto it = user_servers.find(username);
	int serverNum = 1;
	int ret = -1;
	if (it != user_servers.end())
	{
		serverNum = it->second;
		tie(ret, backend_sock) = SetSocket(serverNum);
	}
	while (ret == -1)
	{
		string info = "getserver[SEP]" + username + "\r\n";
		string buffer = Contact_Backend(master_sock, info);
		size_t pos = buffer.find("#");
		if (pos != string::npos)
		{
			string serverNumStr = buffer.substr(pos + 1);
			size_t endPos = serverNumStr.find_first_not_of("0123456789");
			if (endPos != string::npos)
			{
				serverNumStr = serverNumStr.substr(0, endPos);
			}
			serverNum = stoi(serverNumStr);
		}
		if (it != user_servers.end())
		{
			user_servers[username] = serverNum;
		}
		else
		{
			user_servers.insert(make_pair(username, serverNum));
		}
		tie(ret, backend_sock) = SetSocket(serverNum);
	}
	return backend_sock;
}

/**
 * Replace the new line character with <br>
 * @author Zhaozheng Shen
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
 * Converts a URL-encoded string to its corresponding plain text form, including replacing '+' with ' ' and decoding '%XX' escape sequences.
 * @author Zhaozheng Shen
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
 * Read the servers line by line and get all the servers
 * @author Zhaozheng Shen
 */
vector<string> getServers(string filename)
{
	vector<string> servers;

	ifstream file(filename);
	string line;
	while (getline(file, line))
	{
		if (line.empty())
		{
			break;
		}
		servers.push_back(line);
	}
	file.close();
	return servers;
}

/**
 * Add a link to the parent directory in a given path and returns the updated path as a string
 * @author Zhaozheng Shen
 */
string addDirectoryLink(string path)
{
	if (path[0] != '/')
		path = "/" + path;
	int idx = path.find_last_of("/");
	string new_path = "<a href=\"changeDirectory?path=..\">" +
					  path.substr(0, idx + 1) + "</a>" +
					  path.substr(idx + 1, path.length());
	return new_path;
}