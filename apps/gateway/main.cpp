#include "message/MessageJsonCodec.h"
#include "gateway/ConnectionManager.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <variant>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// 将设置为非阻塞sockect
bool set_nonblocking(int fd)
{
    // 读取 Socket 的属性
    const int flags = fcntl(fd, F_GETFL, 0);
    // 增加非阻塞属性
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

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

bool process_data(Connection& connection, const std::uint8_t* data, std::size_t size)
{
    // 获取数据并校验帧
    auto messages = connection.decoder.feed(data, size);
    if (!messages) {
        std::cerr << "protocol error: invalid frame\n";
        return false;
    }
    // 解析报文正文json
    for (const auto& body : *messages) {
        const auto received = message::deserialize_message(body);
        if (!received) {
            std::cerr << "message error: invalid JSON business message\n";
            return false;
        }
        // 校验 STATUS 消息
        std::cout << "Route target: " << device_name(received->dst_id) << '\n';
        if (received->msg_type != message::MessageType::STATUS ||
            received->src_id != message::DeviceId::POWER_ECU ||
            received->dst_id != message::DeviceId::CLUSTER_ECU ||
            !std::holds_alternative<message::StatusPayload>(received->payload)) {
            std::cerr << "message error: unexpected message type or device ID\n";
            return false;
        }
        // 打印业务数据
        const auto& status = std::get<message::StatusPayload>(received->payload);
        std::cout << "STATUS: speed_kmh=" << status.speed_kmh
                  << ", rpm=" << status.rpm
                  << ", coolant_temp_c=" << status.coolant_temp_c
                  << ", engine_status=" << engine_status_name(status.engine_status) << '\n';
    }
    return true;
}

}  // namespace

int main()
{
    // 1、创建TCP通信套接字
    // (使用 IPv4, 使用面向字节流的 TCP, 自动选择对应协议)
    // 1. 创建服务器Socket
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { // 创建失败，会返回负数
        std::cerr << "socket failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (!set_nonblocking(server_fd)) { // 校验是否非阻塞
        std::cerr << "fcntl failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }
    // 2、绑定 ip 和 端口
    // 2. 绑定IP和端口
    // 创建IP和端口
    sockaddr_in address{};
    address.sin_family = AF_INET; //使用 IPv4
    address.sin_port = htons(5000); //设置端口 5000
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //设置 IP 为 127.0.0.1
    // 绑定服务器socket与ip端口
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }
    // 3、开始监听
    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }
    const int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        std::cerr << "epoll_create1 failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }
    epoll_event server_event{};
    server_event.events = EPOLLIN;
    server_event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &server_event) < 0) {
        std::cerr << "epoll_ctl failed: " << std::strerror(errno) << '\n';
        close(epoll_fd);
        close(server_fd);
        return 1;
    }
    ConnectionManager connections;
    std::cout << "Gateway listening on 127.0.0.1:5000" << std::endl;
    // 4、接受客户端连接
    // server_fd：负责监听客户端连接
    // client_fd：负责与已经连接的客户端通信
    // 5、接收数据
    epoll_event events[64];
    while (true) {
        const int ready = epoll_wait(epoll_fd, events, 64, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait failed: " << std::strerror(errno) << '\n';
            break;
        }
        for (int i = 0; i < ready; ++i) {
            const int fd = events[i].data.fd;
            if (fd == server_fd) {
                while (true) {
                    const int client_fd = accept(server_fd, nullptr, nullptr);
                    if (client_fd < 0) {
                        if (errno == EINTR) continue;
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
                        break;
                    }
                    if (!set_nonblocking(client_fd)) {
                        std::cerr << "client fcntl failed: " << std::strerror(errno) << '\n';
                        close(client_fd);
                        continue;
                    }
                    if (!connections.add_connection(client_fd)) {
                        std::cerr << "add_connection failed\n";
                        close(client_fd);
                        continue;
                    }
                    epoll_event client_event{};
                    client_event.events = EPOLLIN | EPOLLRDHUP;
                    client_event.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) < 0) {
                        std::cerr << "client epoll_ctl failed: " << std::strerror(errno) << '\n';
                        connections.remove_connection(client_fd);
                        close(client_fd);
                    }
                }
                continue;
            }
            Connection* connection = connections.find_by_fd(fd);
            if (!connection) continue;
            bool disconnect = false;
            std::uint8_t buffer[256];
            while (true) {
                // recv()用于接收数据
                const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
                if (count > 0) {
                    if (!process_data(*connection, buffer, static_cast<std::size_t>(count))) {
                        disconnect = true;
                        break;
                    }
                } else if (count == 0) {
                    // 接收成功，但客户端断开连接，检查报文完整
                    // 客户端断开连接后，是否存在未处理完的报文数据
                    if (connection->decoder.has_pending_data())
                        std::cerr << "protocol error: incomplete frame at disconnect\n";
                    else
                        std::cout << "Client disconnected\n";
                    disconnect = true;
                    break;
                } else {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    std::cerr << "recv failed: " << std::strerror(errno) << '\n';
                    disconnect = true;
                    break;
                }
            }
            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                if (!disconnect && connection->decoder.has_pending_data())
                    std::cerr << "protocol error: incomplete frame at disconnect\n";
                disconnect = true;
            }
            if (disconnect) {
                if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) < 0)
                    std::cerr << "epoll delete failed: " << std::strerror(errno) << '\n';
                connections.remove_connection(fd);
                close(fd);
            }
        }
    }
    close(epoll_fd);
    close(server_fd);
    return 1;
}
