#include <iostream>

#include "frontend_to_client.h"
#include "httpserver.h"
#include "utils.h"
#include "../common/common_functions.h"

string PATH = "";

using namespace std;

/**
 * Generate the HTML content from file ../UI_interface/{url}.html
 * @author Zhaozheng Shen
 */
string Get_Response(string url)
{
    string filepath = "../UI_interface" + url + ".html";
    ifstream file(filepath);
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
    {
        file.close();
        return oss.str();
    }
    return "";
}

/**
 * Generate a random string for sessionID
 * @author Yigao Fang
 */
string generateRandomString(int length)
{
    const string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dis(0, charset.length() - 1);

    string randomString;
    for (int i = 0; i < length; i++)
    {
        randomString += charset[dis(gen)];
    }
    return randomString;
}

/**
 * Constructor for class MessageToClient, initialize the HTTP response useful infomation
 * @author Zhaozheng Shen
 */
MessageToClient::MessageToClient(string path, string version, int url_tyle, int vflag, int port_number, int server_idx)
{
    this->version = version;
    this->status = 200;
    this->status_msg = "OK";
    this->content_type = "text/html";
    this->url = path;
    this->request_type = url_tyle;
    this->vflag = vflag;
    this->sessionId = "";
    this->server_idx = server_idx;
    PATH = "http://localhost:" + to_string(port_number);
    this->size = 0;
}

/**
 * Generate a new sessionID for current user and store the username sessionID pair to backend
 * @author Yigao Fang
 */
void MessageToClient::generateSessionId(string username, int sockfd)
{
    string sid = generateRandomString(10);
    string info = "put[SEP]" + username + "[SEP]SID[SEP]" + sid + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf;
    ss >> buf;
    if (buf == "+OK")
    {
        this->sessionId = sid;
    }
    else
    {
        handleUnauthorizedResponse();
    }
}

/**
 * Return TRUE if the incoming username sessionID pair corresponds to backend stored pair
 * @author Yigao Fang
 */
bool MessageToClient::compareSessionId(pair<string, string> cookie, int sockfd)
{
    string info = "get[SEP]" + cookie.first + "[SEP]SID\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf, sid;
    ss >> buf >> sid;
    if (buf == "+OK" and sid == cookie.second)
    {
        return true;
    }
    handleUnauthorizedResponse();
    return false;
}

/**
 * Handle client static GET request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleStaticResponse()
{
    this->body = Get_Response(url);
}

/**
 * Handle client SWITCHOFF request (switchoff the current frontend server) and generate response (303)
 * @author Zhaozheng Shen
 */
void MessageToClient::handleSwitchoffRedirect()
{
    this->status = REDIRECT_PORT_CODE;
    this->redirect_location = string(PATH) + "/";
}

/**
 * Handle client ROOT request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleRootResponse()
{
    // redirect (303) to login page
    this->status = REDIRECT_CODE;
    this->redirect_location = string(PATH) + "/login";
    this->body = Get_Response("/login");
}

/**
 * Handle client LOGIN request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleLoginVerifyResponse(int sockfd)
{
    // get parameter: username, password
    regex pattern("username=(.*)&password=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string username = urlDecode(match[1]);
    string password = match[2];
    if (this->vflag)
    {
        cout << "Username: " << username << endl;
        cout << "Password: " << password << endl;
    }
    this->user = username;

    string info = "get[SEP]" + username + "[SEP]PASSWORD\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf, true_pwd;
    ss >> buf >> true_pwd;

    if (buf == "+OK" && password == true_pwd)
    {
        // correct password, redirect (303) to homePage
        this->user = username;
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/homePage";
        this->body = Get_Response("/homePage");

        generateSessionId(username, sockfd);
    }
    else
    {
        // invalid username or password, render login failed page
        this->body = Get_Response("/loginFailed");
    }
}

/**
 * Handle client REGISTER request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleRegisterResponse(int sockfd)
{
    // get parameter: username, password
    regex pattern("username=(.*)&password=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string username = urlDecode(match[1]);
    string password = match[2];

    if (vflag)
    {
        cout << "Username: " << username << endl;
        cout << "Password: " << password << endl;
    }

    string info = "register[SEP]" + username + "[SEP]" + password + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf, true_pwd;
    ss >> buf >> true_pwd;

    if (buf == "+OK")
    {
        // register successfully, redirect (303) to homePage
        this->user = username;
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/homePage";
        this->body = Get_Response("/homePage");

        generateSessionId(username, sockfd);
    }
    else
    {
        // failed to create new account, render register failed page
        this->body = Get_Response("/registerFailed");
    }
}

/**
 * Handle client GET WEBMAIL request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleWebMailResponse(int sockfd)
{
    string info = "listemail[SEP]" + this->user + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string status, msg, line;
    ss >> status;
    while (getline(ss, line))
        msg += line;
    if (status == "+OK")
    {
        // load the inbox data and insert into raw html
        this->body = Get_Response("/webmail");
        vector<string> email_list;
        string data_in_html;
        int idx = 0;
        size_t pos = 0;
        string button;
        // string& last_email;
        while ((pos = msg.find_first_of(string(DELIMITER))) != string::npos)
        {
            if (idx % 4 == 0)
            {
                string tmphtml;
                tmphtml = "<tr>\r\n";
                email_list.push_back(tmphtml);
            }
            // data_in_html += "<tr>\r\n";

            if (idx % 4 == 2)
            {
                // data_in_html += "<td>" + msg.substr(0, pos) + "</td>\r\n";
                string &last_email = email_list.back();
                last_email += "<td>" + msg.substr(0, pos) + "</td>\r\n";
            }
            else if (idx % 4 != 3)
            {
                // data_in_html += "<td>" + urlDecode(msg.substr(0, pos)) + "</td>\r\n";
                string &last_email = email_list.back();
                last_email += "<td>" + urlDecode(msg.substr(0, pos)) + "</td>\r\n";
            }
            else
            {
                string &last_email = email_list.back();
                button = "<a class=\"tableButton\" href=\"openEmail?id=";
                button += msg.substr(0, pos);
                button += "\">Open</a>";
                last_email += "<td>" + button + "</td>\r\n";
                last_email += "</tr>\r\n";
            }
            msg = msg.substr(pos + string(DELIMITER).size());
            idx += 1;
        }
        // data_in_html
        for (auto it = email_list.rbegin(); it != email_list.rend(); ++it)
        {
            data_in_html += *it;
        }

        pos = this->body.find(HTML_REPLACE_TAG);
        if (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_REPLACE_TAG), data_in_html);
        }

        pos = this->body.find(HOME_PAGE_INSERT_USERNAME);
        if (pos != string::npos)
        {
            this->body.replace(pos, strlen(HOME_PAGE_INSERT_USERNAME), this->user);
        }
    }
    else
    {
        // display inbox failed
        this->body = Get_Response("/displayInboxFailed");
    }
}

/**
 * Handle client POST WRITEMAIL request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleWriteMailResponse(int sockfd)
{
    // get parameter: to, subject, content
    regex pattern("to=(.*)&subject=(.*)&content=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string receiver = urlDecode(match[1]);
    string subject = match[2];
    string content = match[3];
    if (vflag)
    {
        cout << "Receiver: " << receiver << endl;
        cout << "Subject: " << subject << endl;
        cout << "Content: " << content << endl;
    }

    int receiver_sock = socket(AF_INET, SOCK_STREAM, 0);
    receiver_sock = SetBackendSocket(receiver);

    string info = "putemail[SEP]" + this->user + "[SEP]" + receiver + "[SEP]" + subject + "[SEP]" + content + "\r\n";
    string buffer = Contact_Backend(receiver_sock, info);
    stringstream ss(buffer);
    string status, msg;
    ss >> status >> msg;

    if (status == "+OK")
    {
        // return and update webmail html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/webmail";
        handleWebMailResponse(sockfd);
    }
    else
    {
        // write email failed!
        this->body = Get_Response("/writeEmailFailed");
    }
}

/**
 * Handle client OPENMAIL request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleOpenMailResponse(int sockfd)
{
    // get parameter: id
    regex pattern("id=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string id = match[1];
    if (vflag)
    {
        cout << "ID: " << id << endl;
    }

    string info = "openemail[SEP]" + this->user + "[SEP]" + id + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string status, msg, line;
    ss >> status;
    while (getline(ss, line))
        msg += line;

    if (status == "+OK")
    {
        // return and update webmail html
        this->body = Get_Response("/openEmail");

        string subject, from, mail_time;
        size_t pos = 0;
        int idx = 0;
        while ((pos = msg.find_first_of(string(DELIMITER))) != string::npos)
        {
            if (idx == 0)
                subject = msg.substr(0, pos);
            else if (idx == 1)
                from = msg.substr(0, pos);
            else if (idx == 2)
                mail_time = msg.substr(0, pos);
            msg = msg.substr(pos + string(DELIMITER).size());
            idx += 1;
        }

        string content = msg;
        string newcontent = urlDecode(content);
        while (content != newcontent)
        {
            content = newcontent;
            newcontent = urlDecode(content);
        }

        pos = this->body.find(HTML_EMAIL_SUBJECT);
        while (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_EMAIL_SUBJECT), urlDecode(subject));
            pos = this->body.find(HTML_EMAIL_SUBJECT);
        }
        // HTML_EMAIL_TIME, mail_time
        pos = this->body.find(HTML_EMAIL_TIME);
        while (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_EMAIL_TIME), urlDecode(mail_time));
            pos = this->body.find(HTML_EMAIL_TIME);
        }

        pos = this->body.find(HTML_EMAIL_FROM);
        while (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_EMAIL_FROM), urlDecode(from));
            pos = this->body.find(HTML_EMAIL_FROM);
        }

        pos = this->body.find(HTML_USERNAME_TAG);
        while (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_USERNAME_TAG), urlDecode(this->user));
            pos = this->body.find(HTML_USERNAME_TAG);
        }

        pos = this->body.find(HTML_EMAIL_CONTENT);
        if (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_EMAIL_CONTENT), msg);
        }

        pos = this->body.find(HTML_EMAIL_CONTENT_W_STYLE);
        if (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_EMAIL_CONTENT_W_STYLE), content);
        }

        pos = this->body.find(HTML_EMAIL_ID);
        if (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_EMAIL_ID), urlDecode(id));
        }
    }
    else
    {
        // open email failed!
        this->body = Get_Response("/openEmailFailed");
    }
}

/**
 * Handle client DELETEMAIL request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleDeleteMailResponse(int sockfd)
{
    // get parameter: id
    regex pattern("id=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string id = urlDecode(match[1]);
    if (vflag)
    {
        cout << "ID: " << id << endl;
    }

    string info = "deleteemail[SEP]" + this->user + "[SEP]" + id + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string status, msg;
    ss >> status >> msg;

    if (status == "+OK")
    {
        // return and update webmail html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/webmail";
        handleWebMailResponse(sockfd);
    }
    else
    {
        // delete email failed!
        this->body = Get_Response("/deleteEmailFailed");
    }
}

/**
 * Handle client GET STORAGE request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleStorageResponse(int sockfd)
{

    string info = "listfile[SEP]" + this->user + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    string status = buffer.substr(0, buffer.find(" "));
    string msg = buffer.substr(buffer.find(" ") + 1);
    if (status == "+OK")
    {
        // load the storage data and insert into raw html
        this->body = Get_Response("/storage");

        vector<string> directory_list, file_list;
        string cur_file;
        string cur_dir;
        int idx = 0;
        size_t pos = 0;
        while ((pos = msg.find_first_of(string(DELIMITER))) != string::npos)
        {
            if (idx == 0)
            {
                cur_dir = msg.substr(0, pos);
            }
            else if (idx % 2 == 1)
            {
                cur_file = msg.substr(0, pos);
            }
            else
            {
                if (msg.substr(0, 1) == "1")
                    directory_list.push_back(cur_file);
                else
                    file_list.push_back(cur_file);
            }

            msg = msg.substr(pos + string(DELIMITER).size());
            idx += 1;
        }

        string button;
        string directory_in_html, file_in_html;

        // write directories and files into html
        for (string data : directory_list)
        {
            directory_in_html += "<tr>\r\n";

            button = "<a uk-tooltip=\"Open\" class=\"uk-button-text uk-text-left\" href=\"changeDirectory?path=";
            button += data;
            button += "\">" + data + "</a>";
            directory_in_html += "<td>" + button + "</td>\r\n";

            // add move button
            button = "<div id=\"moveFunction\" class=\"modal\"></div>\r\n";
            button += "<a uk-tooltip=\"Move\" class=\"uk-link uk-link-heading\" onclick=\"moveButtonClick('";
            button += data + "')\"><span data-uk-icon=\"icon: move\"></span></a>";
            directory_in_html += "<td>" + button + "</td>\r\n";

            // add remove button
            button = "<a uk-tooltip=\"Remove\" class=\"uk-link uk-link-heading\" href=\"remove?name=";
            button += data;
            button += "\"><span data-uk-icon=\"icon: trash\"></span></a>";
            directory_in_html += "<td>" + button + "</td>\r\n";

            // add rename button
            button = "<div id=\"renameFunction\" class=\"modal\"></div>\r\n";
            button += "<a uk-tooltip=\"Rename\" class=\"uk-link uk-link-heading\" onclick=\"renameButtonClick('";
            button += data + "')\"><span data-uk-icon=\"icon: pencil\"></span></a>";
            directory_in_html += "<td>" + button + "</td>\r\n";

            directory_in_html += "</tr>\r\n";
        }

        for (string data : file_list)
        {
            file_in_html += "<tr>\r\n";
            file_in_html += "<td>" + data + "</td>\r\n";

            // add download button
            button = "<a uk-tooltip=\"Download\" class=\"uk-link uk-link-heading\" download=\"";
            button += data;
            button += "\" href=\"download?name=";
            button += data;
            button += "\"><span data-uk-icon=\"icon: download\"></span></a>";
            file_in_html += "<td>" + button + "</td>\r\n";

            // add move button
            button = "<div id=\"moveFunction\" class=\"modal\"></div>\r\n";
            button += "<a uk-tooltip=\"Move\" class=\"uk-link uk-link-heading\" onclick=\"moveButtonClick('";
            button += data + "')\"><span data-uk-icon=\"icon: move\"></span></a>";
            file_in_html += "<td>" + button + "</td>\r\n";

            // add remove button
            button = "<a uk-tooltip=\"Remove\" class=\"uk-link uk-link-heading\" href=\"remove?name=";
            button += data;
            button += "\"><span data-uk-icon=\"icon: trash\"></span></a>";
            file_in_html += "<td>" + button + "</td>\r\n";

            // add rename button
            button = "<div id=\"renameFunction\" class=\"modal\"></div>\r\n";
            button += "<a uk-tooltip=\"Rename\" class=\"uk-link uk-link-heading\" onclick=\"renameButtonClick('";
            button += data + "')\"><span data-uk-icon=\"icon: pencil\"></span></a>";
            file_in_html += "<td>" + button + "</td>\r\n";

            file_in_html += "</tr>\r\n";
        }

        pos = this->body.find(HTML_STORAGE_DIRECTORY);
        if (pos != string::npos)
        {
            cur_dir = addDirectoryLink(cur_dir);
            this->body.replace(pos, strlen(HTML_STORAGE_DIRECTORY), cur_dir);
        }

        pos = this->body.find(HTML_STORAGE_DIRECTORY_TAG);
        if (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_STORAGE_DIRECTORY_TAG), directory_in_html);
        }

        pos = this->body.find(HTML_STORAGE_FILE_TAG);
        if (pos != string::npos)
        {
            this->body.replace(pos, strlen(HTML_STORAGE_FILE_TAG), file_in_html);
        }
    }
    else
    {
        // display current file and folder failed!
        this->body = Get_Response("/displayStorageFailed");
    }
}

/**
 * Handle client POST UPLOAD request and generate response
 * @author Zhaozheng Shen, Yigao Fang
 */
void MessageToClient::handleUploadResponse(int sockfd, string filename, vector<string> file_content, string file_type)
{
    this->file_content = file_content;
    this->content_type = file_type;

    // step 1: get encoded strings total_encode and put them into vector encoded_strings
    string total_string = "";
    for (const auto &substring : file_content)
        total_string += substring;
    string total_encode = encode(reinterpret_cast<const unsigned char *>(total_string.c_str()), total_string.length());
    int total_encoded_size = total_encode.size();
    cout << total_encoded_size << endl;

    vector<string> encoded_strings;
    for (int i = 0; i < total_encode.size(); i += FILE_CHUNK_SIZE)
    {
        string cur_encoded = total_encode.substr(i, min(FILE_CHUNK_SIZE, total_encoded_size - i));
        encoded_strings.push_back(cur_encoded);
    }

    // step 2: send encoded strings to backend: first uploadfile message, then trunk by trunk
    string info = "uploadfile[SEP]" + this->user + "[SEP]" + filename + "[SEP]" + file_type + "[SEP]" + to_string(encoded_strings.size());
    for (const auto &substring : encoded_strings)
    {
        const char *buf = substring.c_str();
        info += "[SEP]" + to_string(strlen(buf));
    }
    info += "\r\n";
    const char *forward_msg = info.c_str();
    cout << "send to backend " << forward_msg << endl;
    Send_Message(sockfd, forward_msg);

    for (const auto &substring : encoded_strings)
    {
        const char *buf = substring.c_str();
        CommonFunctions::do_write(sockfd, buf, strlen(buf));
    }

    // step 3: receive response message from backend, display storage page
    string buffer = Receive_Message(sockfd);
    stringstream ss(buffer);
    string status, msg;
    ss >> status >> msg;
    if (status == "+OK")
    {
        // return and update storage html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/storage";
        handleStorageResponse(sockfd);
    }
    else
    {
        // upload failed!
        this->body = Get_Response("/uploadFailed");
    }
}

/**
 * Handle client DOWNLOAD request and generate response
 * @author Zhaozheng Shen, Yigao Fang
 */
void MessageToClient::handleDownloadResponse(int sockfd)
{
    // get parameter: name
    regex pattern("name=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string file_name = match[1];
    if (this->vflag)
        cout << "File name: " << file_name << endl;

    // step 2: send dowloadfile message to backend and receive response
    string info = "downloadfile[SEP]" + this->user + "[SEP]" + file_name + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf, str, content, content_type;
    ss >> buf >> str;

    if (buf == "+OK")
    {
        // step 3: get file type and the number of chunks to read
        string sep = "[SEP]";
        // Parse type
        size_t pos = str.find(sep);
        string content_type = str.substr(0, pos);
        str = str.substr(pos + sep.length());
        // Parse num_chunk
        pos = str.find(sep);
        int num_chunk = stoi(str.substr(0, pos));
        str = str.substr(pos + sep.length());
        // Parse sizes
        vector<int> sizes;
        for (int i = 0; i < num_chunk; i++)
        {
            pos = str.find(sep);
            if (pos == string::npos)
            {
                pos = str.size();
            }
            int curr_size = stoi(str.substr(0, pos));
            sizes.push_back(curr_size);
            if (i < num_chunk - 1)
            {
                str = str.substr(pos + sep.length());
            }
        }

        // step 3: read the chunks into receive_content
        this->size = 0;
        string receive_content = "";
        for (int i = 0; i < sizes.size(); i++)
        {
            char buf[sizes[i] + 1];
            CommonFunctions::do_read(sockfd, buf, sizes[i]);
            buf[sizes[i]] = 0;
            string tmp_content(buf);
            receive_content += tmp_content;
        }

        // step 4: decode the total receive_content and save into file_content
        string actual_content = decode(receive_content);
        cout << actual_content.size() << endl;
        vector<string> file_content;
        this->size = actual_content.size();
        for (int i = 0; i < this->size; i += FILE_CHUNK_SIZE)
        {
            string chunk = actual_content.substr(i, min(FILE_CHUNK_SIZE, this->size - i));
            file_content.push_back(chunk);
        }

        this->content_type = content_type;
        this->file_content = file_content;
    }
    else
    {
        // download failed!
        this->body = Get_Response("/downloadFailed");
    }
}

/**
 * Handle client CREATEFOLDER request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleCreateFolderResponse(int sockfd)
{
    // get parameter: name
    regex pattern("name=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string file_name = match[1];
    if (this->vflag)
    {
        cout << "File name: " << file_name << endl;
    }

    string info = "makedir[SEP]" + this->user + "[SEP]" + file_name + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string status;
    ss >> status;

    if (status == "+OK")
    {
        // return and update storage html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/storage";
        handleStorageResponse(sockfd);
    }
    else
    {
        // create folder failed!
        this->body = Get_Response("/createFolderFailed");
    }
}

/**
 * Handle client MOVE request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleMoveResponse(int sockfd)
{
    // get parameter: filname, path
    regex pattern("filname=(.*)&path=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string filename = match[1];
    string dst_dir = urlDecode(match[2]);
    if (this->vflag)
    {
        cout << "File Name: " << filename << endl;
        cout << "New Directory Name: " << dst_dir << endl;
    }

    string info = "move[SEP]" + this->user + "[SEP]" + filename + "[SEP]" + dst_dir + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf;
    ss >> buf;

    if (buf == "+OK")
    {
        // Directory changed successfully, return and update storage html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/storage";
        handleStorageResponse(sockfd);
    }
    else
    {
        // change directory failed!
        this->body = Get_Response("/changeDirectoryFailed");
    }
}

/**
 * Handle client REMOVE request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleRemoveResponse(int sockfd)
{
    // get parameter: name
    regex pattern("name=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string file_name = match[1];
    if (this->vflag)
    {
        cout << "File name: " << file_name << endl;
    }

    string info = "remove[SEP]" + this->user + "[SEP]" + file_name + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf;
    ss >> buf;

    if (buf == "+OK")
    {
        // remove successfully, return and update storage html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/storage";
        handleStorageResponse(sockfd);
    }
    else
    {
        // remove failed!
        this->body = Get_Response("/removeFailed");
    }
}

/**
 * Handle client RENAME request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleRenameResponse(int sockfd)
{
    // get parameter: oldname, newname
    regex pattern("oldname=(.*)&newname=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string oldname = match[1];
    string newname = match[2];
    if (this->vflag)
    {
        cout << "Old name: " << oldname << endl;
        cout << "New name: " << newname << endl;
    }

    string info = "rename[SEP]" + this->user + "[SEP]" + oldname + "[SEP]" + newname + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf;
    ss >> buf;

    if (buf == "+OK")
    {
        // rename successfully, return and update storage html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/storage";
        handleStorageResponse(sockfd);
    }
    else
    {
        // rename failed!
        this->body = Get_Response("/renameFailed");
    }
}

/**
 * Handle client CHANGEDIRECTORY request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleChangeDirectoryResponse(int sockfd)
{
    // get parameter: path
    regex pattern("path=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string dst_dir = urlDecode(match[1]);
    if (this->vflag)
    {
        cout << "New Directory Name: " << dst_dir << endl;
    }

    string info = "changedir[SEP]" + this->user + "[SEP]" + dst_dir + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf;
    ss >> buf;

    if (buf == "+OK")
    {
        // Directory changed successfully, return and update storage html
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/storage";
        handleStorageResponse(sockfd);
    }
    else
    {
        // change directory failed!
        this->body = Get_Response("/changeDirectoryFailed");
    }
}

/**
 * Handle client SWITCH BACKEND request and generate response
 * @author Yixuan Meng
 */
void MessageToClient::handleSwitchBackendServer(int master_sockfd, int loadbalancer_sockfd)
{
    // get parameter: id
    regex pattern("id=(.*)&status=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string id = match[1];
    string cur_status = match[2];

    if (vflag)
    {
        cout << "switching backend server id: " << id << endl;
        cout << "current status: " << cur_status << endl;
    }

    // find the server to switchoff or restart
    vector<server_data> backend_server_data_list = CommonFunctions::process_config_file("../common/backend_servers.txt", 0);
    server_data s = backend_server_data_list[stoi(id)];
    sockaddr_in server_src = s.forward_src;
    int sfd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
    server_addr.sin_port = htons(s.forward_portno);

    int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == 0)
    {
        string info;
        if (cur_status == "1")
        {
            info = "switchoff\r\n";
            if (this->vflag)
                cout << "switching off" << endl;
        }
        else
        {
            info = "restart\r\n";
            if (this->vflag)
                cout << "switching on" << endl;
        }
        // send the switchoff/restart message to a particular server
        string buffer = Contact_Backend(sfd, info);
        stringstream ss(buffer);
        string buf;
        ss >> buf;

        if (buf == "+OK" && this->vflag)
            cout << "buffer:" << buffer << endl;
        else if (this->vflag)
            cout << "switch failed" << endl;
    }
    else if (this->vflag)
        cout << "connect to backend server failed" << endl;

    // redirect (303) to login page
    this->status = REDIRECT_CODE;
    this->redirect_location = string(PATH) + "/admin";
    this->handleAdminResponse(master_sockfd, loadbalancer_sockfd);
}

/**
 * Handle client SWITCH FRONTEND request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleSwitchFrontendServer(int master_sockfd, int loadbalancer_sockfd, bool *switched_on)
{
    // get parameter: id
    regex pattern("id=(.*)&status=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string id = match[1];
    string cur_status = match[2];

    if (stoi(id) == this->server_idx)
    {
        this->handleSwitchoffRedirect();
        send(master_sockfd, "quit\r\n", strlen("quit\r\n"), 0);
        *switched_on = false;
        // cout << "invalid this page" << endl;
        return;
    }

    vector<server_data> frontend_server_data_list = CommonFunctions::process_config_file("../common/frontend_servers.txt", 0);
    server_data s = frontend_server_data_list[stoi(id)];
    sockaddr_in server_src = s.forward_src;
    int sfd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
    server_addr.sin_port = htons(s.forward_portno);

    int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == 0)
    {
        string info;
        if (cur_status == "1")
        {
            info = "switchoff\r\n";
            if (this->vflag)
                cout << "switching off" << endl;
        }
        else
        {
            info = "restart\r\n";
            if (this->vflag)
                cout << "switching on" << endl;
        }
        string buffer = Contact_Backend(sfd, info);
        stringstream ss(buffer);
        string buf;
        ss >> buf;

        if (buf == "+OK")
        {
            // redirect (303) to login page
            this->status = REDIRECT_CODE;
            this->redirect_location = string(PATH) + "/admin";
            this->handleAdminResponse(master_sockfd, loadbalancer_sockfd);
        }
    }
    else if (this->vflag)
        cout << "connect to frontend server failed" << endl;
}

/**
 * Handle client GET HOMEPAGE request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleHomePage(int sockfd)
{
    this->body = Get_Response("/homePage");
    size_t pos = 0;
    pos = this->body.find(HOME_PAGE_INSERT_USERNAME);
    if (pos != string::npos)
    {
        this->body.replace(pos, strlen(HOME_PAGE_INSERT_USERNAME), this->user);
    }
}

/**
 * Handle client admin request and generate response
 * @author Zhaozheng Shen, Yixuan Meng
 */
void MessageToClient::handleAdminResponse(int master_sockfd, int loadbalancer_sockfd)
{
    if (this->user != "admin@penncloud")
    {
        handleUnauthorizedResponse();
        return;
    }

    vector<string> backend_servers = getServers("../common/backend_servers.txt");
    vector<string> frontend_servers = getServers("../common/frontend_servers.txt");

    this->body = Get_Response("/admin");

    // step1: contact frontend
    string info = "heartbeat\r\n";
    string frontend_heartbead;

    frontend_heartbead = Contact_Backend(loadbalancer_sockfd, info);

    stringstream ss1(frontend_heartbead);
    string buf1, msg;
    ss1 >> buf1 >> msg;
    if (this->vflag)
        cout << msg << endl; //  1,0,0,0
    vector<string> frontend_fields;
    if (buf1 == "+OK")
    {
        frontend_fields = CommonFunctions::split_by_delimiter(msg, ",");
    }
    else if (this->vflag)
        cout << "heartbeat failed" << endl;

    // step2: get heartbeat info
    string buffer = Contact_Backend(master_sockfd, info);
    stringstream ss(buffer);
    string buf;
    ss >> buf;
    if (this->vflag)
        cout << buffer << endl; //  +OK 1,0,0,0,0,0,0
    vector<string> fields;
    if (buf == "+OK")
    {
        fields = CommonFunctions::split_by_delimiter(buffer.substr(4, sizeof(buffer)), ",");
    }
    else if (this->vflag)
        cout << "heartbeat failed" << endl;

    // step3: get raw data
    vector<vector<vector<string>>> raw_data(backend_servers.size());
    set<string> unique_strings;
    string config_file = "../common/backend_servers.txt";
    vector<server_data> backend_server_data_list = CommonFunctions::process_config_file(config_file, 0);

    for (int i = 1; i < backend_server_data_list.size(); i++)
    {
        server_data s = backend_server_data_list[i];
        sockaddr_in server_src = s.forward_src;
        int sfd = socket(PF_INET, SOCK_STREAM, 0);
        struct sockaddr_in server_addr;

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(s.forward_IPaddr);
        server_addr.sin_port = htons(s.forward_portno);

        int ret = connect(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret == 0)
        {
            string info = "rawdata\r\n"; // GET /admin HTTP/1.1
            string buffer = Contact_Backend(sfd, info);
            stringstream ss(buffer);
            string buf;
            ss >> buf;

            string to_extract;
            // ss >> to_extract;
            vector<string> fields;
            if (buf == "+OK")
            {
                to_extract = buffer.substr(4, buffer.length());
                fields = CommonFunctions::split_by_delimiter(to_extract, "[KVS]");
                for (string field : fields)
                {
                    if (field.size() == 0)
                        continue;
                    unique_strings.insert(field);
                    vector<string> row = CommonFunctions::split_by_delimiter(field, "[SEP]");
                    raw_data[i].push_back(row);
                }
            }
            else
            {
                if (this->vflag)
                    cout << "rawdata failed" << endl;
            }
        }
    }

    vector<vector<string>> unique_raw_data;
    for (const auto &unique_str : unique_strings)
    {
        vector<string> row = CommonFunctions::split_by_delimiter(unique_str, "[SEP]");
        unique_raw_data.push_back(row);
    }

    // step4: add html
    string backend_in_html = "";
    // for (string server : backend_servers)
    for (int i = 0; i < backend_servers.size(); i++)
    {
        string server = backend_servers[i];
        backend_in_html += "<tr>\n<th>";
        backend_in_html += server;
        if (i == 0)
            backend_in_html += " (Master)";
        backend_in_html += "</th>\n";

        // backend_in_html += fields[i];
        if (fields[i] == "1")
            backend_in_html += "<th style=\"color: green;\">Running";
        else
            backend_in_html += "<th style=\"color: red;\">Stopped";

        backend_in_html += "</th>\n<th>";
        if (fields[i] == "1")
        {
            string button;
            button = "<a href=\"switchBackendServer?id=";
            button += to_string(i);
            button += "&status=";
            button += fields[i];
            button += "\">Close</a>";
            backend_in_html += button;
        }
        else
        {
            string button;
            button = "<a href=\"switchBackendServer?id=";
            button += to_string(i);
            button += "&status=";
            button += fields[i];
            button += "\">Open</a>";
            backend_in_html += button;
        }
        backend_in_html += "</th>\n</tr>";
    }

    string fronted_in_html = "";
    // cout << frontend_servers.size() << frontend_fields.size() << endl;
    for (int i = 0; i < frontend_servers.size(); i++)
    {
        string server = frontend_servers[i];
        fronted_in_html += "<tr>\n<th>";
        fronted_in_html += server;
        if (i == 0)
            fronted_in_html += " (Load Balancer)";
        fronted_in_html += "</th>\n";

        if (frontend_fields[i] == "1")
            fronted_in_html += "<th style=\"color: green;\">Running";
        else
            fronted_in_html += "<th style=\"color: red;\">Stopped";

        fronted_in_html += "</th>\n<th>";
        if (frontend_fields[i] == "1")
        {
            string button;
            button = "<a href=\"switchFrontendServer?id=";
            button += to_string(i);
            button += "&status=";
            button += frontend_fields[i];
            button += "\">Close</a>";
            fronted_in_html += button;
        }
        else
        {
            string button;
            button = "<a href=\"switchFrontendServer?id=";
            button += to_string(i);
            button += "&status=";
            button += frontend_fields[i];
            button += "\">Open</a>";
            fronted_in_html += button;
        }
        fronted_in_html += "</th>\n</tr>";
    }

    string kvs_in_html = "";
    for (vector<string> row : unique_raw_data)
    {
        kvs_in_html += "<tr>\n<th>";
        kvs_in_html += row[0];
        kvs_in_html += "</th>\n<th>";
        kvs_in_html += row[1];
        kvs_in_html += "</th>\n<th>";
        kvs_in_html += row[2];
        kvs_in_html += "</th>\n</tr>";
    }

    size_t pos = 0;
    pos = this->body.find(HTML_BACKEND_SERVERS);
    if (pos != string::npos)
    {
        this->body.replace(pos, strlen(HTML_BACKEND_SERVERS), backend_in_html);
    }

    pos = this->body.find(HTML_FRONTEND_SERVERS);
    if (pos != string::npos)
    {
        this->body.replace(pos, strlen(HTML_FRONTEND_SERVERS), fronted_in_html);
    }

    pos = this->body.find(HTML_KVS_DATA);
    if (pos != string::npos)
    {
        this->body.replace(pos, strlen(HTML_KVS_DATA), kvs_in_html);
    }
}

/**
 * Handle client CHANGE PASSWORD request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleChangePasswordResponse(int sockfd)
{
    // get parameter: password
    regex pattern("password=(.*)");
    smatch match;
    regex_search(url, match, pattern);
    string password = match[1];
    if (this->vflag)
    {
        cout << "Password: " << password << endl;
    }

    string info = "put[SEP]" + this->user + "[SEP]PASSWORD[SEP]" + password + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    stringstream ss(buffer);
    string buf, true_pwd;
    ss >> buf >> true_pwd;

    if (buf == "+OK")
    {
        // change password successfully, redirect (303) to homePage
        this->status = REDIRECT_CODE;
        this->redirect_location = string(PATH) + "/homePage";
        this->body = Get_Response("/homePage");
    }
    else
    {
        // failed to change password, render change password failed page
        this->body = Get_Response("/changePasswordFailed");
    }
}

/**
 * Handle client LOGOUT request and generate response
 * @author Zhaozheng Shen
 */
void MessageToClient::handleLogoutResponse(int sockfd)
{
    // logout successfully, redirect (303) to login page
    this->status = REDIRECT_CODE;
    string info = "logout[SEP]" + this->user + "\r\n";
    string buffer = Contact_Backend(sockfd, info);
    this->redirect_location = string(PATH) + "/login";
    this->body = Get_Response("/login");
}

/**
 * Handle failure response
 * @author Yigao Fang
 */
void MessageToClient::handleFalureResponse()
{
    this->status = 404;
    this->status_msg = "Not found";
}

/**
 * Handle unauthorized response
 * @author Yigao Fang
 */
void MessageToClient::handleUnauthorizedResponse()
{
    this->status = 401;
    this->status_msg = "Unauthorized";
}

/**
 * Prepare response message giving class information and client request
 * @author Zhaozheng Shen
 */
string MessageToClient::prepareMessage()
{
    if (this->status == REDIRECT_PORT_CODE)
    {
        // handle current frontend server failure response: redirect to load balancer
        string response;
        response += version + " " + to_string(this->status) + "\r\n";
        response += "Content-type: text/html\r\n";
        response += "Content-length: 0\r\n";
        response += "Location: http://localhost:8000\r\n";
        response += "\r\n";
        return response;
    }

    if (this->status == 401 or this->status == 404)
    {
        // handle unauthorized and page not fond response
        string response;
        response += this->version + " ";
        response += to_string(this->status) + " " + this->status_msg + "\r\n";
        response += "Content-type: " + this->content_type + "\r\n";
        response += "Content-length: 0\r\n\r\n";
        return response;
    }

    // handle other responses
    string response;
    response += this->version + " ";
    response += to_string(this->status) + " " + this->status_msg + "\r\n";
    response += "Connection: Keep-Alive\r\n";
    response += "Keep-Alive: timeout=10, max=1000\r\n";
    response += "Content-type: " + this->content_type + "\r\n";
    response += "Content-length: " + to_string(this->body.size()) + "\r\n";

    if (this->request_type == DOWNLOAD)
    {
        response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
        response += "Expires: 0\r\n";
    }

    // set cookie if needed
    if (this->status == REDIRECT_CODE and this->sessionId != "" and (this->request_type == LOGIN or this->request_type == LOGIN_VERIFY or this->request_type == REGISTER))
    {
        response += "Set-Cookie: name=" + this->user + "\r\n";
        response += "Set-Cookie: sessionid=" + this->sessionId + "\r\n";
    }
    if (this->request_type == LOGOUT)
    {
        response += "Set-Cookie: name=" + this->user + "; Expires=Thu, 01 Jan 1970 00:00:00 GMT;\r\n";
        response += "Set-Cookie: sessionid=" + this->sessionId + "; Expires=Thu, 01 Jan 1970 00:00:00 GMT;\r\n";
    }

    // redirect to other servers if needed
    if (this->status == REDIRECT_CODE)
    {
        response += "Location: " + this->redirect_location + "\r\n";
    }
    if (this->vflag)
    {
        cout << response << endl;
    }
    response += "\r\n";
    response += this->body;
    return response;
}

/**
 * Prepare response message for download giving class information
 * @author Yigao Fang
 */
string MessageToClient::downloadMessage()
{
    string response;
    response += this->version + " ";
    response += to_string(this->status) + " " + this->status_msg + "\r\n";
    response += "Connection: Keep-Alive\r\n";
    response += "Keep-Alive: timeout=10, max=1000\r\n";
    response += "Content-type: " + this->content_type + "\r\n";
    response += "Content-length: " + to_string(this->size) + "\r\n";
    response += "\r\n";
    return response;
}

/**
 * Get the private file content information
 * @author Yigao Fang
 */
vector<string> MessageToClient::getFileContent()
{
    return this->file_content;
}
