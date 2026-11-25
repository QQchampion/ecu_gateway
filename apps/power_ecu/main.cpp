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
    // 1、创建套件字
    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    // 2、设置服务器地址
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 3、尝试连接serve
    if (connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "connect failed: " << std::strerror(errno) << '\n';
        close(socket_fd);
        return 1;
    }

    std::cout << "Connected to Gateway" << std::endl;

    // 4、准备发送的数据
    // 封装发送的数据
    const auto message = encode_tcp_frame("speed=60");
    if (!message) {
        std::cerr << "protocol error: frame encoding failed\n";
        close(socket_fd);
        return 1;
    }
    std::size_t sent = 0; // 已发送长度

    // 5、发送数据
    while (sent < message->size()) { // 判断条件：只要没发完，继续发
        // send()用于向服务器发送数据
        // send(套接字、发送具体位置、最多发送字节、默认发送位置)
        const ssize_t count = send(socket_fd, message->data() + sent, message->size() - sent, MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR) continue;
            std::cerr << "send failed: " << std::strerror(errno) << '\n';
            close(socket_fd);
            return 1;
        }
        if (count == 0) {
            std::cerr << "send failed: no bytes sent\n";
            close(socket_fd);
            return 1;
        }
        sent += static_cast<std::size_t>(count);
    }

    // 6、接收数据
    TcpFrameDecoder decoder;
    std::uint8_t buffer[256];
    while (true) {
        const ssize_t count = recv(socket_fd, buffer, sizeof(buffer), 0);
        if (count < 0) { // 接收出现错误
            if (errno == EINTR) continue;
            std::cerr << "recv failed: " << std::strerror(errno) << '\n';
            close(socket_fd);
            return 1;
        }
        if (count == 0) { // 表示客户端已经正常关闭连接
            std::cerr << "Gateway disconnected before complete ACK frame\n";
            close(socket_fd);
            return 1;
        }
        // 接收成功
        // 拆解封装
        auto messages = decoder.feed(buffer, static_cast<std::size_t>(count));
        if (!messages) {
            std::cerr << "protocol error: invalid ACK frame\n";
            close(socket_fd);
            return 1;
        }
        // 解析服务器的ACK回应
        for (const auto& ack : *messages) {
            if (ack != "ACK: received") {
                std::cerr << "unexpected ACK: " << ack << '\n';
                close(socket_fd);
                return 1;
            }
            std::cout << ack << '\n';
            close(socket_fd);
            return 0;
        }
    }
}
