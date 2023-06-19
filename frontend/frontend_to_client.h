#ifndef FRONTEND_TO_CLIENT_H
#define FRONTEND_TO_CLIENT_H

#include <string>
#include <random>

#define REDIRECT_CODE 303
#define REDIRECT_PORT_CODE 301
#define HTML_REPLACE_TAG "{REPLACE WITH DATA}"
#define HTML_USERNAME_TAG "{REPLACE WITH USERNAME}"
#define HTML_STORAGE_DIRECTORY_TAG "{REPLACE WITH DIRECTORY}"
#define HTML_STORAGE_FILE_TAG "{REPLACE WITH FILE}"
#define HTML_STORAGE_DIRECTORY "{CURRENT DIRECTORY}"
#define HTML_EMAIL_ID "{EMAIL ID}"
#define HTML_EMAIL_SUBJECT "{EMAIL SUBJECT}"
#define HTML_EMAIL_TIME "{EMAIL_TIME}"
#define HTML_EMAIL_FROM "{EMAIL FROM}"
#define HTML_EMAIL_CONTENT "{EMAIL CONTENT}"
#define HTML_EMAIL_CONTENT_W_STYLE "{EMAIL STYLE CONTENT}"
#define HTML_BACKEND_SERVERS "{REPLACE WITH BACKEND SERVERS}"
#define HTML_FRONTEND_SERVERS "{REPLACE WITH FRONTEND SERVERS}"
#define HTML_KVS_DATA "{REPLACE WITH KVS DATA}"
#define DELIMITER "[SEP]"
#define HOME_PAGE_INSERT_USERNAME "{HOME_PAGE_INSERT_USERNAME}"

using namespace std;

/**
 * Class to handle client request, contact backend for data, prepare response message, and send reponse back to client
 * @author Zhaozheng Shen
 */
class MessageToClient
{
private:
    string version;
    string content_type; // default: "text/html"
    int status;          // default: 200
    string status_msg;   // default: OK
    string body;
    string url;
    int request_type;
    bool vflag;
    string user;
    string redirect_location;
    string sessionId;
    int server_idx;
    vector<string> file_content;
    int size;

public:
    MessageToClient(string path, string version, int url_type, int vflag, int port_number, int server_idx);
    void handleStaticResponse();
    void handleSwitchoffRedirect();
    void handleRootResponse();
    void handleLoginVerifyResponse(int sockfd);
    void handleRegisterResponse(int sockfd);
    void handleWebMailResponse(int sockfd);
    void handleWriteMailResponse(int sockfd);
    void handleOpenMailResponse(int sockfd);
    void handleDeleteMailResponse(int sockfd);
    void handleStorageResponse(int sockfd);
    void handleUploadResponse(int sockfd, string filename, vector<string> file_content, string file_type);
    void handleDownloadResponse(int sockfd);
    void handleCreateFolderResponse(int sockfd);
    void handleMoveResponse(int sockfd);
    void handleRemoveResponse(int sockfd);
    void handleRenameResponse(int sockfd);
    void handleChangeDirectoryResponse(int sockfd);
    void handleHomePage(int sockfd);
    void handleAdminResponse(int master_sockfd, int loadbalancer_sockfd);
    void handleSwitchBackendServer(int master_sockfd, int loadbalancer_sockfd);
    void handleSwitchFrontendServer(int master_sockfd, int loadbalancer_sockfd, bool *switched_on);
    void handleChangePasswordResponse(int sockfd);
    void handleLogoutResponse(int sockfd);
    void handleFalureResponse();
    void handleUnauthorizedResponse();

    string prepareMessage();
    string downloadMessage();
    vector<string> getFileContent();

    void generateSessionId(string username, int sockfd);
    bool compareSessionId(pair<string, string> cookie, int sockfd);

    /**
     * Print the request type and the request url
     * @author Zhaozheng Shen
     */
    void printValue()
    {
        cout << "find: " << request_type << " " << url << endl;
    }

    /**
     * Set the user info
     * @author Zhaozheng Shen
     */
    void setUser(string user)
    {
        this->user = user;
    }

    /**
     * Set the session ID
     * @author Yigao Fang
     */
    void setSessionId(string sessionId)
    {
        this->sessionId = sessionId;
    }

    /**
     * Get the request type
     * @author Zhaozheng Shen
     */
    int getRequestType()
    {
        return request_type;
    }
};

#endif
