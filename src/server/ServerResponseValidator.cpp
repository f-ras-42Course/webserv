#include "server/ServerResponseValidator.hpp"
#include <map>
#include <iostream>
#include <sys/stat.h>
#include <filesystem>

ServerResponseValidator::ServerResponseValidator(const std::vector<std::shared_ptr<Location>>& locations, const std::string& root) : locations_(locations), root_(root) {};

ServerResponseValidator::~ServerResponseValidator() {};

/**
 * @brief checks if the HTTPVersion is HTTP/1.1
 * 
 * @param http_version the http version the client used in his request
 * @return true if http_version is "HTTP/1.1"
 * @return false of http_version is not "HTTP/1.1"
 */
bool ServerResponseValidator::checkHTTPVersion(std::string& http_version)
{
    if (http_version == "HTTP/1.1")
        return true;
    return false;
}

/**
 * @brief checks if a location is known by the server
 * and if there is a more global defined return version defined before the complete version
 * 
 * @param token_location the chunked location
 * @param file_path what will hold the location of the file
 * @param location_it what will hold the itterator point of the found and used location
 * @return RVR_OK when done,
 * @return RVR_NOT_FOUND if no location matches the request location
 * @return RVR_RETURN if a redirect location was defined before the precise location
 */
e_responeValReturn ServerResponseValidator::checkLocations(std::vector<std::string>& token_location, std::string& file_path, std::vector<std::shared_ptr<Location>>::const_iterator& location_it)
{
    std::map<size_t, std::string> found_location;
    size_t token_size = token_location.size();
    setPosibleLocation(token_size, token_location, found_location);

    if (!found_location.empty())
    {
        size_t most_precise =  0;
        std::map<size_t, std::string>::iterator fl_it = found_location.begin();
        std::map<size_t, std::string>::iterator fl_ite = found_location.end();
        while (fl_it != fl_ite)
        {
            if (fl_it->first > most_precise)
                most_precise = fl_it->first;
            ++fl_it;
        }

        for (size_t i = 0; i < most_precise; ++i)
        {
            try
            {
                if (found_location.at(i).compare("return"))
                    return RVR_RETURN;
            }
            catch(const std::exception& e) {(void)e;}
        }

        std::string full_request;
        if (token_size == 1 && token_location.at(0) == "/")
            full_request = "/";
        else
            for (size_t l = 0; l < token_size; ++l)
                full_request.append("/" + token_location.at(l));
        if (found_location.at(most_precise) == full_request || found_location.at(most_precise).find(".") != std::string::npos)
        {
            location_it = std::next(locations_.begin(), most_precise);
            if (location_it->get()->getPath() == "/")
               file_path = "/";
            else
                file_path = root_ + location_it->get()->getIndex();
        }
    }
    if (file_path.empty())
        return RVR_NOT_FOUND;
    return RVR_OK;
}

/**
 * @brief checks if the requested method is allowed on the requested location
 * 
 * @param location_it a pointer to the location with all its info from the config file
 * @param method the requested method
 * @return RVR_OK if method is allowed,
 * @return RVR_METHOD_NOT_ALLOWED if method is not allowed,
 * @return RVR_BUFFER_NOT_EMPTY buffer error
 */
e_responeValReturn ServerResponseValidator::checkAllowedMethods(std::vector<std::shared_ptr<Location>>::const_iterator& location_it, std::string& method)
{
    std::vector<std::string>::const_iterator method_it = location_it->get()->getAllowedMethods().begin();
    std::vector<std::string>::const_iterator method_ite = location_it->get()->getAllowedMethods().end();

    while (method_it != method_ite)
    {
        if (method_it->empty())
        {
            std::cerr << "handle respone request buffer not empty\n";
            return RVR_BUFFER_NOT_EMPTY;
        }
        if (method_it->compare(method) == 0)
            break;
        ++method_it;
    }
    if (method_it == method_ite)
        return RVR_METHOD_NOT_ALLOWED;
    return RVR_OK;
}

/**
 * @brief checks if the file exists and if we have perission of reading it
 * 
 * @param file_path the path to the file
 * @param location_it info on the current location
 * @return RVR_OK when found and we have permission
 * @return RVR_NO_FILE_PERMISSION if we dont have permission to read the file
 * @return RVR_FOUND_AT_ROOT if the found was not in the folder but was found at the root of the website dirctory
 * @return RVR_AUTO_INDEX_ON the file cannot be found and auto indexing is on
 */
e_responeValReturn ServerResponseValidator::checkFile(std::string& file_path, std::vector<std::shared_ptr<Location>>::const_iterator& location_it)
{
    if (fileExists(file_path))
    {
        if (filePermission(file_path))
        {
            std::cout << "file_path: " << file_path << std::endl;
            return RVR_OK;
        }
        else
            return RVR_NO_FILE_PERMISSION;
    }
    else if (location_it->get()->getAutoindex())
        return RVR_AUTO_INDEX_ON;
    else if (fileExists(root_ + location_it->get()->getIndex()))
    {
        if (filePermission(root_ + location_it->get()->getIndex()))
        {
            file_path = root_ + location_it->get()->getIndex();
            return RVR_FOUND_AT_ROOT;
        }
        else
            return RVR_NO_FILE_PERMISSION;
    }
    else
    {
        std::cerr << "file_path: \"" << file_path << "\" not found\n";
        return RVR_NOT_FOUND;
    }
}

/**
 * @brief checks if we have read permission on the file
 * 
 * @param path the location to the file
 * @return true if we have read permission for other on the file
 * @return false if  we do not have read permission for the onter on the file
 */
bool ServerResponseValidator::filePermission(const std::string& path)
{
    struct stat buffer;
    stat(path.c_str(), &buffer);
    return buffer.st_mode & S_IROTH;
}

/**
 * @brief checks if the path is a directory and that we have the right permisions for it
 * 
 * @param location_it itterator to the current location
 * @return RVR_SHOW_DIRECTORY if we need to do directory listing
 * @return RVR_NOT_FOUND if the directory is not found
 * @return RVR_NO_FILE_PERMISSION if we don't have the permission to read the directory
 */
e_responeValReturn ServerResponseValidator::checkAutoIndexing(std::vector<std::shared_ptr<Location>>::const_iterator& location_it)
{
    std::string path = root_ + location_it->get()->getPath();
    if (!isDirectory(path))
        return RVR_NOT_FOUND;
    if (!filePermission(path))
        return RVR_NO_FILE_PERMISSION;
    return RVR_SHOW_DIRECTORY;
}

/**
 * @brief checks if path is a directory
 * 
 * @param path location to the directory
 * @return true if path is a directory
 * @return false if path is not a directory
 */
bool ServerResponseValidator::isDirectory(std::string& path)
{
    return std::filesystem::is_directory(path);
}

/**
 * @brief gets the root folder of the server
 * 
 * @return a string holding the location to the root of the  server
 */
const std::string& ServerResponseValidator::getRoot() const
{
    return root_;
}

// private functions

/**
 * @brief loops to the chunked location and checks it against all known locations
 * 
 * @param token_size how many entries toke_location has
 * @param token_location the chunked sourse from the client request
 * @param found_location will hold the location that have matches to the chunked source
 */
void ServerResponseValidator::setPosibleLocation(size_t token_size, std::vector<std::string>& token_location, std::map<size_t, std::string>& found_location)
{
    size_t location_size = locations_.size();
    if (token_size > 0 && token_location.at(token_size - 1).find(".") != std::string::npos)
    {
        token_location.insert(token_location.begin(), token_location.at(token_size - 1));
        ++token_size;
    }
    if (token_size == 1 && token_location.at(0) == "/")
        found_location.insert(std::pair<size_t, std::string>(0, "/"));
    else
    {
        for (size_t i = 0; i < token_size; ++i)
        {
            size_t current = token_size - i;
            std::string loc = "";
            for (size_t j = 0; j < current; ++j)
                loc.append("/" + token_location.at(j));
            for (size_t j = 0; j < location_size; ++j)
            {
                try
                {
                    if (locations_.at(j).get()->getPath().compare(loc) == 0)
                        found_location.insert(std::pair<size_t, std::string>(j, locations_.at(j).get()->getPath()));
                }
                catch (std::exception& e) {(void)e;}
            }
        }
    }
}

/**
 * @brief checks if the file exists
 * 
 * @param path bath to the file
 * @return true if path is a file
 * @return false if path is not a file
 */
bool ServerResponseValidator::fileExists(const std::string& path)
{
    struct stat buffer;
    return stat(path.c_str(), &buffer) == 0 && S_ISREG(buffer.st_mode);
}