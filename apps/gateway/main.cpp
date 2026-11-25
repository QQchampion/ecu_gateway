#include "protocol/TcpFrameCodec.h"
#include "protocol/TcpFrameDecoder.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
    // 1、创建TCP通信套接字
    // (使用 IPv4, 使用面向字节流的 TCP, 自动选择对应协议)
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { // 创建失败，会返回负数
        std::cerr << "socket failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    // 2、绑定 ip 和 端口
    sockaddr_in address{};
    address.sin_family = AF_INET; //使用 IPv4
    address.sin_port = htons(5000); //设置端口 5000
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //设置 IP 为 127.0.0.1
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 3、开始监听
    if (listen(server_fd, 1) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    std::cout << "Gateway listening on 127.0.0.1:5000" << std::endl;

    // 4、接受客户端连接
    // server_fd：负责监听客户端连接
    // client_fd：负责与已经连接的客户端通信
    const int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
        std::cerr << "accept failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 5、接收数据
    TcpFrameDecoder decoder;
    std::uint8_t buffer[256];
    while (true) {
        // recv()用于接收数据
        const ssize_t count = recv(client_fd, buffer, sizeof(buffer), 0);
        if (count < 0) {
            // 表示接收出现错误
            if (errno == EINTR) continue;
            std::cerr << "recv failed: " << std::strerror(errno) << '\n';
            close(client_fd);
            close(server_fd);
            return 1;
        }
        if (count == 0) {
            // 表示客户端已经正常关闭连接
            std::cerr << "client disconnected before complete frame\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }
        // 调用解析传输帧函数decoder.feed()
        auto messages = decoder.feed(buffer, static_cast<std::size_t>(count));
        if (!messages) {  // 解析后报文错误
            std::cerr << "protocol error: invalid frame\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }
        for (const auto& message : *messages) { // 一次解析会有多条报文，故用for
                std::cout << "Received: " << message << '\n';
                if (message != "speed=60") continue;

                // 6、准备发送回应ACK
                // 6.1、封装ACK
                const auto ack = encode_tcp_frame("ACK: received");
                if (!ack) { // 封装ACK失败
                    std::cerr << "protocol error: ACK encoding failed\n";
                    close(client_fd);
                    close(server_fd);
                    return 1;
                }
                // 6.2、发送ACK
                std::size_t sent = 0;
                while (sent < ack->size()) {
                    const ssize_t written = send(client_fd, ack->data() + sent, ack->size() - sent, MSG_NOSIGNAL);
                    if (written < 0) { // 失败
                        if (errno == EINTR) continue;
                        std::cerr << "send failed: " << std::strerror(errno) << '\n';
                        close(client_fd);
                        close(server_fd);
                        return 1;
                    }
                    if (written == 0) { // 未发送
                        std::cerr << "send failed: no bytes sent\n";
                        close(client_fd);
                        close(server_fd);
                        return 1;
                    }
                    sent += static_cast<std::size_t>(written);
                }
                close(client_fd);
                close(server_fd);
                return 0;
        }
    }
}
