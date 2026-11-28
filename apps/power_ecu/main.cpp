#include "protocol/TcpFrameCodec.h"
#include "message/MessageJsonCodec.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// 发送消息机制
bool send_message(int fd, const message::Message& message)
{
    // 3.json序列化
    // 1. json序列化
    const auto json = message::serialize_message(message);
    if (!json) return false;
    // 4.TCP封装
    // 2. TCP封装信息
    const auto frame = encode_tcp_frame(*json);
    if (!frame) return false;
    // 3. 发送信息的字节
    std::size_t offset = 0; // 已发送长度
    while (offset < frame->size()) { // 判断条件：只要没发完，继续发
        // send()用于向服务器发送数据
        // send(套接字、发送具体位置、最多发送字节、默认发送位置)
        const ssize_t count = send(fd, frame->data() + offset, frame->size() - offset, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}
}

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
    // 4. 构造业务消息
    // 创建心跳信息
    const message::Message heartbeat{message::DeviceId::POWER_ECU,
        message::DeviceId::GATEWAY, message::MessageType::HEARTBEAT, 1,
        message::HeartbeatPayload{}};
    if (!send_message(socket_fd, heartbeat)) {
        std::cerr << "heartbeat send failed\n";
        close(socket_fd);
        return 1;
    }

    // 创建 状态上报消息
    // 1.创建StatusPayload
        // 创建StatusPayload
    const message::StatusPayload status{60.0, 2000, 85.0, message::EngineStatus::RUNNING};
    // 2.创建Message对象
        // 创建Message对象
    message::Message status_message{
        message::DeviceId::POWER_ECU,
        message::DeviceId::CLUSTER_ECU,
        message::MessageType::STATUS,
        2,
        status
    };

    while (true) {
    // 5、发送数据
    if (!send_message(socket_fd, status_message)) {
            std::cerr << "STATUS send failed\n";
            close(socket_fd);
            return 1;
    }

    // 信息发送序号是否为最大值
    if (status_message.seq == std::numeric_limits<std::uint32_t>::max()) break;
    ++status_message.seq;
    sleep(1);
    }
    close(socket_fd);
    return 0;
}
