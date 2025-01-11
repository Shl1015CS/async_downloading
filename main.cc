#include "http_downloader.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "用法: " << argv[0] << " <URL> <输出文件>" << std::endl;
        return 1;
    }

    try {
        HttpDownloader downloader;
        downloader.download(argv[1], argv[2]);
        std::cout << "下载完成！" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 