#include "protocol/TcpFrameCodec.h"
#include "message/MessageJsonCodec.h"
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

    // 4、构造并序列化 STATUS 业务消息
    // 4、构造业务消息
    // 1.创建StatusPayload
    const message::StatusPayload status{60.0, 2000, 85.0, message::EngineStatus::RUNNING};
    // 2.创建Message对象
    const message::Message status_message{
        message::DeviceId::POWER_ECU,
        message::DeviceId::CLUSTER_ECU,
        message::MessageType::STATUS,
        1,
        status
    };
    // 3.json序列化
    const auto json_text = message::serialize_message(status_message);
    if (!json_text) {
        std::cerr << "message error: JSON serialization failed\n";
        close(socket_fd);
        return 1;
    }
    // 4.TCP封装
    const auto frame = encode_tcp_frame(*json_text);
    if (!frame) {
        std::cerr << "protocol error: frame encoding failed\n";
        close(socket_fd);
        return 1;
    }
    std::size_t sent = 0; // 已发送长度

    // 5、发送数据
    while (sent < frame->size()) { // 判断条件：只要没发完，继续发
        // send()用于向服务器发送数据
        // send(套接字、发送具体位置、最多发送字节、默认发送位置)
        const ssize_t count = send(socket_fd, frame->data() + sent, frame->size() - sent, MSG_NOSIGNAL);
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

    close(socket_fd);
    return 0;
}
