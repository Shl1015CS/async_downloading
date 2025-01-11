#include "http_downloader.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdexcept>

HttpDownloader::HttpDownloader() : epoll_fd(-1), sock_fd(-1), fp(nullptr) {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error("创建epoll失败");
    }
}

HttpDownloader::~HttpDownloader() {
    if (epoll_fd != -1) close(epoll_fd);
    if (sock_fd != -1) close(sock_fd);
    if (fp) fclose(fp);
}

void HttpDownloader::parseUrl(const std::string& url) {
    size_t pos = url.find("://");
    if (pos == std::string::npos) {
        throw std::runtime_error("URL格式错误");
    }

    size_t host_start = pos + 3;
    size_t path_start = url.find("/", host_start);
    
    if (path_start == std::string::npos) {
        host = url.substr(host_start);
        path = "/";
    } else {
        host = url.substr(host_start, path_start - host_start);
        path = url.substr(path_start);
    }
}

void HttpDownloader::download(const std::string& url, const std::string& output) {
    parseUrl(url);
    output_file = output;
    
    struct hostent *he = gethostbyname(host.c_str());
    if (!he) {
        throw std::runtime_error("无法解析主机名");
    }

    sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_fd == -1) {
        throw std::runtime_error("创建socket失败");
    }

    int flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);
    memcpy(&server_addr.sin_addr, he->h_addr, he->h_length);

    int ret = connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        throw std::runtime_error("连接失败");
    }

    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLIN | EPOLLET;
    ev.data.fd = sock_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev) < 0) {
        throw std::runtime_error("epoll_ctl失败");
    }

    fp = fopen(output_file.c_str(), "wb");
    if (!fp) {
        throw std::runtime_error("无法创建输出文件");
    }

    eventLoop();
}

void HttpDownloader::eventLoop() {
    struct epoll_event events[1];
    bool request_sent = false;
    bool headers_received = false;
    std::string response_headers;
    int timeout = 5000;
    
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, 1, timeout);
        
        if (nfds == -1) {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait失败");
        }
        
        if (nfds == 0) {
            throw std::runtime_error("操作超时");
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP) {
                throw std::runtime_error("socket错误");
            }

            if (events[i].events & EPOLLOUT && !request_sent) {
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
                    throw std::runtime_error("连接失败");
                }
                
                sendRequest();
                request_sent = true;
                
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = sock_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock_fd, &ev);
            }
            
            if (events[i].events & EPOLLIN) {
                char buffer[4096];
                while (true) {
                    ssize_t n = read(sock_fd, buffer, sizeof(buffer));
                    
                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        throw std::runtime_error("读取数据失败");
                    }
                    
                    if (n == 0) {
                        if (!headers_received) {
                            throw std::runtime_error("连接过早关闭");
                        }
                        return;
                    }

                    if (!headers_received) {
                        response_headers.append(buffer, n);
                        size_t header_end = response_headers.find("\r\n\r\n");
                        
                        if (header_end != std::string::npos) {
                            if (response_headers.find("HTTP/1.1 200") == std::string::npos &&
                                response_headers.find("HTTP/1.0 200") == std::string::npos) {
                                throw std::runtime_error("HTTP响应错误: " + 
                                    response_headers.substr(0, response_headers.find("\r\n")));
                            }
                            
                            headers_received = true;
                            fwrite(buffer + header_end + 4, 1, 
                                  n - (header_end + 4), fp);
                        }
                    } else {
                        fwrite(buffer, 1, n, fp);
                    }
                    
                    fflush(fp);
                }
            }
        }
    }
}

void HttpDownloader::sendRequest() {
    std::string request = 
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Connection: close\r\n"
        "User-Agent: Mozilla/5.0\r\n"
        "Accept: */*\r\n"
        "\r\n";
    
    ssize_t total = 0;
    size_t len = request.length();
    
    while (total < len) {
        ssize_t sent = write(sock_fd, request.c_str() + total, len - total);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            throw std::runtime_error("发送请求失败");
        }
        total += sent;
    }
} 