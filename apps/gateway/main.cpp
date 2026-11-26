#include "message/MessageJsonCodec.h"
#include "protocol/TcpFrameDecoder.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <variant>
#include <sys/socket.h>
#include <unistd.h>

namespace {

const char* device_name(message::DeviceId id)
{
    switch (id) {
        case message::DeviceId::POWER_ECU: return "Power ECU";
        case message::DeviceId::BCM_ECU: return "BCM ECU";
        case message::DeviceId::CLUSTER_ECU: return "Cluster ECU";
        case message::DeviceId::GATEWAY: return "Gateway";
        case message::DeviceId::EXTERNAL_CLIENT: return "External Client";
    }
    return "Unknown";
}

const char* engine_status_name(message::EngineStatus status)
{
    switch (status) {
        case message::EngineStatus::STOPPED: return "STOPPED";
        case message::EngineStatus::STARTING: return "STARTING";
        case message::EngineStatus::RUNNING: return "RUNNING";
        case message::EngineStatus::FAULT: return "FAULT";
    }
    return "UNKNOWN";
}

}  // namespace

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
            if (decoder.has_pending_data()) {
                std::cerr << "protocol error: incomplete frame at disconnect\n";
                close(client_fd);
                close(server_fd);
                return 1;
            }
            std::cout << "Client disconnected\n";
            close(client_fd);
            close(server_fd);
            return 0;
        }
        // 调用解析传输帧函数decoder.feed()
        auto messages = decoder.feed(buffer, static_cast<std::size_t>(count));
        if (!messages) {  // 解析后报文错误
            std::cerr << "protocol error: invalid frame\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }
        for (const auto& body : *messages) {
            const auto received = message::deserialize_message(body);
            if (!received) {
                std::cerr << "message error: invalid JSON business message\n";
                close(client_fd);
                close(server_fd);
                return 1;
            }
            std::cout << "Route target: " << device_name(received->dst_id) << '\n';
            if (received->msg_type != message::MessageType::STATUS ||
                received->src_id != message::DeviceId::POWER_ECU ||
                received->dst_id != message::DeviceId::CLUSTER_ECU ||
                !std::holds_alternative<message::StatusPayload>(received->payload)) {
                std::cerr << "message error: unexpected message type or device ID\n";
                close(client_fd);
                close(server_fd);
                return 1;
            }
            const auto& status = std::get<message::StatusPayload>(received->payload);
            std::cout << "STATUS: speed_kmh=" << status.speed_kmh
                      << ", rpm=" << status.rpm
                      << ", coolant_temp_c=" << status.coolant_temp_c
                      << ", engine_status=" << engine_status_name(status.engine_status)
                      << '\n';
        }
    }
}
