#ifndef HTTP_DOWNLOADER_H
#define HTTP_DOWNLOADER_H

#include <string>
#include <sys/epoll.h>
#include <stdio.h>
#include <nghttp2/nghttp2.h>

class HttpDownloader {
private:
    int epoll_fd;
    int sock_fd;
    std::string host;
    std::string path;
    std::string output_file;
    FILE* fp;
    // HTTP/2 member
    nghttp2_session *session;
    bool is_http2;
    void parseUrl(const std::string& url);
    void eventLoop();
    void sendRequest();
    
    // HTTP/2 download
    void initHttp2Session();
    void submitHttp2Request();
    static ssize_t sendCallback(nghttp2_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data);
    static int onHeaderCallback(nghttp2_session *session,
                              const nghttp2_frame *frame,
                              const uint8_t *name, size_t namelen,
                              const uint8_t *value, size_t valuelen,
                              uint8_t flags, void *user_data);
    static int onDataChunkRecvCallback(nghttp2_session *session,
                                     uint8_t flags, int32_t stream_id,
                                     const uint8_t *data, size_t len,
                                     void *user_data);

public:
    HttpDownloader();
    ~HttpDownloader();
    void download(const std::string& url, const std::string& output);
};

#endif // HTTP_DOWNLOADER_H 