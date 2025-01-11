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
#include <nghttp2/nghttp2.h>

HttpDownloader::HttpDownloader() 
    : epoll_fd(-1), sock_fd(-1), fp(nullptr), 
      session(nullptr), is_http2(false) {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error("creat_epoll_fail");
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
        throw std::runtime_error("URL_type_error");
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
        throw std::runtime_error("can not analyze host");
    }

    sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_fd == -1) {
        throw std::runtime_error("init_socket_error");
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
        throw std::runtime_error("error_fopen");
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
            throw std::runtime_error("epoll_wait_fail");
        }
        
        if (nfds == 0) {
            throw std::runtime_error("runtime over");
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP) {
                throw std::runtime_error("socket_fail");
            }

            if (events[i].events & EPOLLOUT && !request_sent) {
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
                    throw std::runtime_error("connect_error");
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
                        throw std::runtime_error("read_data_fail");
                    }
                    
                    if (n == 0) {
                        if (!headers_received) {
                            throw std::runtime_error("connect_early_close");
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

void HttpDownloader::initHttp2Session() {
    nghttp2_session_callbacks *callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    
    nghttp2_session_callbacks_set_send_callback(callbacks, sendCallback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, onHeaderCallback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
        callbacks, onDataChunkRecvCallback);
    
    nghttp2_session_client_new(&session, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    
    // 发送连接前言
    std::string preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    write(sock_fd, preface.c_str(), preface.length());
    
    // 发送设置帧
    nghttp2_settings_entry iv[1] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
    };
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, iv, 1);
}

void HttpDownloader::submitHttp2Request() {
    const nghttp2_nv hdrs[] = {
        {(uint8_t*)":method", (uint8_t*)"GET", 6, 3, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t*)":path", (uint8_t*)path.c_str(), 5, path.length(), NGHTTP2_NV_FLAG_NONE},
        {(uint8_t*)":scheme", (uint8_t*)"https", 7, 5, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t*)":authority", (uint8_t*)host.c_str(), 10, host.length(), NGHTTP2_NV_FLAG_NONE}
    };
    
    nghttp2_submit_request(session, NULL, hdrs, 4, NULL, this);
    nghttp2_session_send(session);
}

// HTTP/2 回调函数实现
ssize_t HttpDownloader::sendCallback(nghttp2_session *session, const uint8_t *data,
                                   size_t length, int flags, void *user_data) {
    HttpDownloader *downloader = static_cast<HttpDownloader*>(user_data);
    return write(downloader->sock_fd, data, length);
}

int HttpDownloader::onHeaderCallback(nghttp2_session *session,
                                   const nghttp2_frame *frame,
                                   const uint8_t *name, size_t namelen,
                                   const uint8_t *value, size_t valuelen,
                                   uint8_t flags, void *user_data) {
    // 处理响应头
    return 0;
}

int HttpDownloader::onDataChunkRecvCallback(nghttp2_session *session,
                                          uint8_t flags, int32_t stream_id,
                                          const uint8_t *data, size_t len,
                                          void *user_data) {
    HttpDownloader *downloader = static_cast<HttpDownloader*>(user_data);
    fwrite(data, 1, len, downloader->fp);
    return 0;
} 