#ifndef HTTP_DOWNLOADER_H
#define HTTP_DOWNLOADER_H

#include <string>
#include <sys/epoll.h>
#include <stdio.h>

class HttpDownloader {
private:
    int epoll_fd;
    int sock_fd;
    std::string host;
    std::string path;
    std::string output_file;
    FILE* fp;

    void parseUrl(const std::string& url);
    void eventLoop();
    void sendRequest();

public:
    HttpDownloader();
    ~HttpDownloader();
    void download(const std::string& url, const std::string& output);
};

#endif // HTTP_DOWNLOADER_H 