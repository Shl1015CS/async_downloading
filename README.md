# 异步 HTTP 下载器 （网上学的HTTP2.0协议实现）

## 项目简介
这是一个基于 epoll 的异步 HTTP 下载器实现，使用非阻塞 I/O 和事件驱动模型来高效下载 HTTP 文件。

## 核心实现原理

### 异步 I/O 设计
1. epoll 事件驱动
   - 使用 epoll_create1 创建 epoll 实例
   - 采用边缘触发(EPOLLET)模式减少系统调用
   - 通过 epoll_wait 等待 I/O 事件

2. 非阻塞 Socket
   - 创建非阻塞 socket
   - 使用 fcntl 设置 O_NONBLOCK 标志
   - 实现异步连接和数据传输

### 工作流程
1. 连接阶段
   ```cpp
   sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
   connect(sock_fd, ...);  // 非阻塞连接
   epoll_ctl(..., EPOLLOUT | EPOLLIN | EPOLLET, ...);  // 监听连接状态
   ```

2. 请求发送
   - 连接成功(EPOLLOUT)时发送 HTTP GET 请求
   - 切换到读取模式(EPOLLIN)等待响应

3. 数据接收
   - 接收 HTTP 头部
   - 解析响应状态码
   - 循环读取响应体并写入文件

### 状态管理
- request_sent: 标记 HTTP 请求是否已发送
- headers_received: 标记是否已接收到 HTTP 头
- 通过状态机确保下载流程的正确性

### 错误处理
1. 连接错误
   ```cpp
   if (ret < 0 && errno != EINPROGRESS) {
       throw std::runtime_error("连接失败");
   }
   ```

2. 超时处理
   ```cpp
   int nfds = epoll_wait(epoll_fd, events, 1, timeout);
   if (nfds == 0) {
       throw std::runtime_error("操作超时");
   }
   ```

3. HTTP 错误
   - 检查响应状态码
   - 验证数据完整性
   - 处理连接异常中断

## 编译和使用

### 编译
```bash
make
```

### 运行
```bash
./http_downloader <URL> <输出文件>
```

## 技术要点

### epoll 的优势
1. 高效的事件通知机制
2. 避免频繁的系统调用
3. 支持大量并发连接
4. 精确的事件触发控制

### 非阻塞 I/O 的好处
1. 提高程序响应性
2. 避免资源浪费
3. 实现真正的异步操作
4. 更好的资源利用率

### 边缘触发(ET)模式
1. 减少重复通知
2. 降低系统开销
3. 提高事件处理效率
4. 需要完整处理所有数据

## 代码结构
```
async_downloading/
├── http_downloader.h    // 类声明和接口定义
├── http_downloader.cc   // 具体实现
├── main.cc             // 主程序入口
└── Makefile           // 编译规则
```

## HTTP/2 协议实现

### HTTP/2 核心特性
1. 二进制分帧
   - 将消息分解为更小的帧
   - 实现了更高效的消息传输
   - 本项目使用 nghttp2 库处理二进制帧

2. 多路复用
   ```cpp
   nghttp2_settings_entry iv[1] = {
       {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
   };
   ```
   - 在单个 TCP 连接上并行处理多个请求/响应
   - 解决了 HTTP/1.1 的队头阻塞问题
   - 通过流(Stream)标识符区分不同请求

3. 服务器推送
   - 服务器可以主动推送相关资源
   - 减少客户端请求次数
   - 提高资源加载效率

4. 头部压缩
   ```cpp
   const nghttp2_nv hdrs[] = {
       {(uint8_t*)":method", (uint8_t*)"GET", 6, 3, NGHTTP2_NV_FLAG_NONE},
       {(uint8_t*)":path", (uint8_t*)path.c_str(), 5, path.length(), NGHTTP2_NV_FLAG_NONE},
       // ...
   };
   ```
   - 使用 HPACK 算法压缩头部
   - 减少带宽使用
   - 提高传输效率

### 项目中的 HTTP/2 实现
1. 会话管理
   ```cpp
   void initHttp2Session() {
       nghttp2_session_callbacks *callbacks;
       nghttp2_session_callbacks_new(&callbacks);
       // 设置各种回调函数
       nghttp2_session_client_new(&session, callbacks, this);
   }
   ```

2. 流程控制
   - 初始化 HTTP/2 会话
   - 发送连接前言(Preface)
   - 设置初始流控制参数
   - 处理 SETTINGS 帧

## 异步和并发实现

### 异步特性
异步是指程序在执行某个操作时，不需要等待该操作完成就可以继续执行其他任务。

本项目的异步体现：
1. 非阻塞 Socket 操作
   ```cpp
   sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
   fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);
   ```
   - connect() 不会阻塞等待连接建立
   - read()/write() 不会阻塞等待数据
   - 可以立即处理其他任务

2. 事件驱动模型
   ```cpp
   int nfds = epoll_wait(epoll_fd, events, 1, timeout);
   if (events[i].events & EPOLLOUT) {
       // 处理可写事件
   }
   if (events[i].events & EPOLLIN) {
       // 处理可读事件
   }
   ```
   - 使用 epoll 监听 I/O 事件
   - 事件触发时才处理相应操作
   - 避免轮询等待

3. 回调处理
   ```cpp
   static ssize_t sendCallback(...);
   static int onHeaderCallback(...);
   static int onDataChunkRecvCallback(...);
   ```
   - 通过回调函数处理异步操作的结果
   - 不同类型的数据有独立的处理流程
   - 提高程序的响应性

### 并发特性
并发是指多个操作在同一时间段内同时进行。

本项目的并发体现：
1. HTTP/2 多路复用
   ```cpp
   nghttp2_settings_entry iv[1] = {
       {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
   };
   ```
   - 单个连接支持并发处理多个请求
   - 不同的流可以并行传输
   - 提高资源利用效率

2. epoll 多事件处理
   ```cpp
   struct epoll_event events[1];
   while (true) {
       int nfds = epoll_wait(epoll_fd, events, 1, timeout);
       for (int i = 0; i < nfds; i++) {
           // 并发处理多个事件
       }
   }
   ```
   - 同时监听多个文件描述符
   - 可以并发处理读写事件
   - 支持高并发连接

### 异步和并发的区别
1. 概念区别
   - 异步：关注操作的执行方式（不等待）
   - 并发：关注操作的执行时机（同时进行）

2. 实现方式
   - 异步：通过回调、事件通知等机制
   - 并发：通过多路复用、多线程等技术

3. 优势互补
   - 异步提高程序响应性
   - 并发提高系统吞吐量
   - 结合使用可以达到最佳性能