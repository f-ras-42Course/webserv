#include "server/ServerResponseHandler.hpp"
#include <iostream>
#include <sys/types.h>
#include <dirent.h>
#include <sstream>
#include <sys/socket.h>
#include <fstream>
#include <sys/stat.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

ServerResponseHandler::ServerResponseHandler(const std::vector<std::shared_ptr<Location>>& locations, const std::string& root, const std::map<uint16_t, std::string>& error_map) : SRV_(locations, root), error_pages_(error_map)
{
    stdout_pipe_[0] = -1;
    stdout_pipe_[1] = -1;
}

ServerResponseHandler::~ServerResponseHandler() {};

void ServerResponseHandler::setStdoutPipe(int stdout_pipe[])
{
    stdout_pipe_[0] = stdout_pipe[0];
}

/**
 * @brief checks if everything from the request is good. The right http version,
 * Is the method alowed on the location the client wants.
 * Check for redirects.
 * Is auto indexing on and the index page missing.
 * And send the response to the client
 * 
 * @param client_fd the file descriptor of the client
 * @param client_data the data of the client from the request is send
 * @param locations all locations known to the server and there info
 * @return RVR_OK if all info is good and response has been send,
 * @return SRH_INCORRECT_HTTP_VERSION if the HTTPVersion in the request is not supported
 */
e_server_request_return ServerResponseHandler::handleResponse(int client_fd, s_client_data client_data, std::vector<std::shared_ptr<Location>> locations)
{
    std::string file_path;
    std::vector<std::shared_ptr<Location>>::const_iterator location_it = locations.begin();

    if (!SRV_.checkHTTPVersion(client_data.http_version))
        return SRH_INCORRECT_HTTP_VERSION;
    
    std::vector<std::string> token_location = sourceChunker(client_data.request_source);
    e_responeValReturn nr = SRV_.checkLocations(token_location, file_path, location_it);
    if (nr != RVR_OK)
        return handleReturns(client_fd, nr, client_data);

    nr = SRV_.checkAllowedMethods(location_it, client_data.request_method);
    if (nr != RVR_OK)
        return handleReturns(client_fd, nr, client_data);

    nr = SRV_.checkFile(file_path, location_it);
    if (nr != RVR_OK)
    {
        if (nr == RVR_AUTO_INDEX_ON)
        {
            nr = SRV_.checkAutoIndexing(location_it);
            if (nr != RVR_SHOW_DIRECTORY)
            {
                if (nr == RVR_NOT_FOUND)
                    return setupResponse(client_fd, "404 Not Found", 404, client_data);
                else if (nr == RVR_NO_FILE_PERMISSION)
                    return setupResponse(client_fd, "403 Forbidden", 403, client_data);
            }
            std::string body;
            e_server_request_return response = buildDirectoryResponse(file_path, body);
            if (response != SRH_OK)
                return handleReturns(client_fd, RVR_DIR_FAILED, client_data);
            return sendResponse(client_fd, "200 Ok", body, client_data, true);
        }
        else
            return handleReturns(client_fd, nr, client_data);
    }
    return setupResponse(client_fd, "200 OK", 200, client_data, file_path);
}

/**
 * @brief depending on the code it sets up the response
 * If the code is a error code it looks if the config has a error page with the coresponding code,
 * or that a fall back error page is needed
 * 
 * @param client_fd the file descriptor of the client
 * @param status_text the text that the code uses
 * @param code the code we send in the return
 * @param data the request data from the client
 * @param location the location where the page is located (can be empty)
 * @return SRH_OK when done
 */
e_server_request_return ServerResponseHandler::setupResponse(int client_fd, std::string status_text, uint32_t code, s_client_data data, std::string location)
{
    if (code == 200)
    {
        sendResponse(client_fd, status_text, location, data);
        return SRH_OK;
    }
    else
    {
        std::map<uint16_t, std::string>::const_iterator error_page = error_pages_.find(code);
        if (error_page == error_pages_.end())
        {
            std::string fall_back = "example/errorPages/";
            fall_back.append(std::to_string(code));
            fall_back.append(".html");
            sendResponse(client_fd, status_text, fall_back, data);
        }
        else
        {
            location.insert(0UL, SRV_.getRoot());
            sendResponse(client_fd, status_text, location + "/" + error_page->second, data);
        }
    }
    return SRH_OK;
}

/**
 * @brief handle buffers being writen to the standard output and standard error and put then in log files
 * 
 * @param fd file descriptor of stdout_pipe[0] or stderr_pipe[0] 
 */
void ServerResponseHandler::handleCoutErrOutput(int fd)
{
    ssize_t bytes_recieved = 0;
    std::string request_buffer;
    char buffer[BUFFER_SIZE];
    while ((bytes_recieved = read(fd, buffer, BUFFER_SIZE - 1)) > 0)
    {
        buffer[bytes_recieved] = '\0';
        request_buffer.append(buffer, bytes_recieved);
    }
    if (fd == stdout_pipe_[0])
        request_buffer.insert(0, "[Captured stdcout]: ");
    else
        request_buffer.insert(0, "[Captured stdcerr]: ");
    logMsg(request_buffer.c_str(), fd);
}

// private functions

/**
 * @brief handles different returnn messages
 * 
 * @param client_fd the file descriptor of the client
 * @param nr what e_responseValRetun varlue is used
 * @param data the request data from the user
 * @return SRH_OK when done
 */
e_server_request_return ServerResponseHandler::handleReturns(int client_fd, e_responeValReturn nr, s_client_data data)
{
    switch (nr)
    {
        case RVR_RETURN:
            setupResponse(client_fd, "301 Moved Permanently", 301, data);
            break;
        case RVR_NOT_FOUND:
            setupResponse(client_fd, "404 Not Found", 404, data);
            break;
        case RVR_BUFFER_NOT_EMPTY:
            setupResponse(client_fd, "500 Internal Server Error", 500, data);
            break;
        case RVR_METHOD_NOT_ALLOWED:
            setupResponse(client_fd, "400 Bad Request", 400, data);
            break;
        case RVR_NO_FILE_PERMISSION:
            setupResponse(client_fd, "403 Forbidden", 403, data);
            break;
        case RVR_DIR_FAILED:
            setupResponse(client_fd, "500 Internal Server Error", 500, data);
            break;
        default:
            std::cerr << "unkown respone validator error " << nr << '\n';
            setupResponse(client_fd, "500 Internal Server Error", 500, data);
            break;
    }
    return SRH_OK;
}
/**
 * @brief makes the response body if directory listing needs to be shown
 * 
 * @param path from where to look and start directory listing
 * @param body what will hold the body data
 * @return SRH_OK when done,
 * @return SRH_OPEN_DIR_FAILED if opendir() failed
 */
e_server_request_return ServerResponseHandler::buildDirectoryResponse(const std::string& path, std::string& body)
{
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr)
    {
        std::cerr << "failed to open directory!\n";
        return SRH_OPEN_DIR_FAILED;
    }
    body.append("<!DOCTYPE html><html><body><h1>Directory Listing for ");
    body.append(path);
    body.append("</h1><ul>");
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        body.append("<li>");
        body.append(entry->d_name);
        body.append("</li>");
    }
    body.append("</ul></body></html>");
    closedir(dir);
    return SRH_OK;
}

/**
 * @brief Builds the response header and the body to be send to the client
 * 
 * @param client_fd file descriptor of the client
 * @param status the string holding the status of the response 
 * @param file_location where the file holding the respone is locaded
 * @param data the request data from the client
 * @param d_list true if we need to show directory listing
 * @return SRH_OK when done,
 * @return SRH_SEND_ERROR when send() failes,
 * @return SRH_FSTREAM_ERROR when file stream failed to open 
 */
e_server_request_return ServerResponseHandler::sendResponse(int client_fd, const std::string& status, const std::string& file_location, s_client_data& data, bool d_list)
{
    bool content = false;
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n";
    response << "Connection: close\r\n";

    if (d_list)
    {
        response << "Content-Type: " << getContentType("x.html") << "\r\n";
        response << "Content-Length: " << file_location.size() << "\r\n\r\n";
        response << file_location;
        if (send(client_fd, response.str().c_str(), response.str().size(), 0) < 0)
            return SRH_SEND_ERROR;
        return SRH_OK;
    }

    response << "Content-Type: " << getContentType(file_location) << "\r\n";
    std::ifstream file_stream("." + file_location, std::ios::binary);
    if (!file_stream.is_open())
    {
        std::cerr << "file_stream open: " << file_location << std::endl;
        return SRH_FSTREAM_ERROR;
    }

    if (data.chunked)
    {
        response << "Transfer-Encoding: chunked\r\n\r\n";
        if (send(client_fd, response.str().c_str(), response.str().size(), 0) < 0)
            return SRH_SEND_ERROR;
        if (sendChunkedResponse(client_fd, file_stream)!= SRH_OK)
            return SRH_SEND_ERROR;
    }
    else
    {
        if (file_stream.good())
        {
            content = true;
            file_stream.seekg(0, std::ios::end);
            std::streamsize file_size = file_stream.tellg();
            file_stream.seekg(0, std::ios::beg);
            response << "Content-Length: " << file_size << "\r\n\r\n";
        }
        else
            response << "Content-Length: 0\r\n\r\n";
        if (send(client_fd, response.str().c_str(), response.str().size(), 0) < 0)
            return SRH_SEND_ERROR;
        if (content)
        {
            if (sendFile(client_fd, file_stream) != SRH_OK)
                return SRH_SEND_ERROR;
        }
    }
    return SRH_OK;
}

/**
 * @brief based on what file we are sending the content type for the respose is set
 * 
 * @param file_path the file path to the file being send
 * @return a string with the content type
 */
std::string ServerResponseHandler::getContentType(const std::string& file_path)
{
    size_t dot_pos = file_path.rfind('.');
    if (dot_pos == std::string::npos)
        return "application/octet-stream"; // Default binary type
    std::string extension = file_path.substr(dot_pos);
    if (extension == ".html")
        return "text/html";
    if (extension == ".css")
        return "text/css";
    if (extension == ".js")
        return "application/javascript";
    if (extension == ".json")
        return "application/json";
    if (extension == ".png")
        return "image/png";
    if (extension == ".jpg" || extension == ".jpeg")
        return "image/jpeg";
    if (extension == ".gif")
        return "image/gif";
    if (extension == ".svg")
        return "image/svg+xml";
    if (extension == ".txt")
        return "text/plain";
    return "application/octet-stream"; // Default for unknown types
}

/**
 * @brief chunks the response data and send it chunk by chunk to the client
 * 
 * @param client_fd file descriptor of the client
 * @param file_stream the file stream holding the respone for the client
 * @return SRH_OK when done,
 * @return SRH_SEND_ERROR is send() fails
 */
e_server_request_return ServerResponseHandler::sendChunkedResponse(int client_fd, std::ifstream& file_stream)
{
    char buffer[BUFFER_SIZE];
    while (file_stream.read(buffer, BUFFER_SIZE) || file_stream.gcount() > 0)
    {
        std::ostringstream chunk;
        chunk << std::hex << file_stream.gcount() << "\r\n"; // chunk size in hex
        if (send(client_fd, chunk.str().c_str(), chunk.str().size(), 0) < 0)
            return SRH_SEND_ERROR;
        if (send(client_fd, buffer, file_stream.gcount(), 0) < 0)
            return SRH_SEND_ERROR;
        if (send(client_fd, "\r\n\r\n", 2, 0) < 0)
            return SRH_SEND_ERROR;
    }
    if (send(client_fd, "0\r\n\r\n", 5, 0) < 0)
        return SRH_SEND_ERROR;
    return SRH_OK;
}

/**
 * @brief sends the body with the info from the file to the client
 * 
 * @param client_fd the file descriptor of the client
 * @param file_stream tje stream holding the response for the cloent
 * @return SRH_OK when done,
 * @return SRH_SEND_ERROR when send() fails
 */
e_server_request_return ServerResponseHandler::sendFile(int client_fd, std::ifstream& file_stream)
{
    char buffer[BUFFER_SIZE];
    while(file_stream.read(buffer, BUFFER_SIZE) || file_stream.gcount() > 0)
    {
        if (send(client_fd, buffer, file_stream.gcount(), MSG_NOSIGNAL) < 0)
            return SRH_SEND_ERROR;
    }
    return SRH_OK;
}

/**
 * @brief puts the request source from the client in chunks to check if previous defined less precise have a redirect
 * 
 * @param source the request source from the client
 * @return a vector with the source in chunks split on '/'
 */
std::vector<std::string> ServerResponseHandler::sourceChunker(std::string& source)
{
    char delimiter = '/';
    std::vector<std::string> result;
    std::string token;
    if (source == "/")
    {
        result.push_back("/");
        return result;
    }
    for (char ch : source)
    {
        if (ch == delimiter)
        {
            if (!token.empty())
            {
                result.push_back(token);
                token.clear();
            }
        }
        else 
            token += ch;
    }
    if (!token.empty())
        result.push_back(token);
    return result;
}

/**
 * @brief logs messages from the standard output and standard error to log files
 * 
 * @param msg the message we want to log
 * @param fd either the file descriptor of stdout_pipe[0] or stderr_pipe[0]
 */
void ServerResponseHandler::logMsg(const char* msg, int fd)
{
    std::string file;
    if (fd == stdout_pipe_[0])
        file = STANDARD_LOG_FILE;
    else
        file = STANDARD_ERROR_LOG_FILE;
    if (mkdir("logs", 0777) < 0 && errno != EEXIST)
    {
        std::cerr << "mkdir() error: " << strerror(errno) << std::endl;
        return;
    }
    file.insert(0,"logs/");
    int file_fd = open(file.c_str(), O_CREAT | O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR);
    write(file_fd, msg, strlen(msg));
    close(file_fd);
}