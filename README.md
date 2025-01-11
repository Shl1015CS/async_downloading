# 异步 HTTP 下载器

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

## 后续优化方向
1. 添加 HTTPS 支持
2. 实现断点续传
3. 添加下载进度显示
4. 支持并发下载多个文件
5. 添加下载速度限制功能