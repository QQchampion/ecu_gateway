#include "message/MessageJsonCodec.h"
#include "gateway/ConnectionManager.h"
#include "gateway/TcpSender.h"
#include "protocol/TcpFrameCodec.h"
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

// 关闭 Socket 的套件字
void close_client(int epoll_fd, ConnectionManager& connections, int fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) < 0)
        std::cerr << "epoll delete failed: " << std::strerror(errno) << '\n';
    connections.remove_connection(fd);
    close(fd);
}

// 处理接收到消息
bool process_data(int epoll_fd, ConnectionManager& connections, int fd, Connection& connection,
                  const std::uint8_t* data, std::size_t size)
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
        // 信息为 HEARTBEAT 心跳
        if (received->msg_type == message::MessageType::HEARTBEAT) {
            // 校验 HEARTBEAT 心跳 是否合法
            if (received->dst_id != message::DeviceId::GATEWAY || // 接收对象不是网关
                !std::holds_alternative<message::HeartbeatPayload>(received->payload) || // 消息不是心跳
                (connection.device_id && *connection.device_id != received->src_id) || // 防止冒充
                (!connection.device_id && !connections.register_device(fd, received->src_id))) { // 未登记设备注册失败
                std::cerr << "protocol error: invalid device registration\n";
                return false;
            }
            // 校验成功，登记设备
            std::cout << "Registered: " << device_name(received->src_id) << std::endl;
            continue;
        }
        if (!connection.device_id || *connection.device_id != received->src_id) {
            std::cerr << "protocol error: unregistered or mismatched source\n";
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

        // 网关 转发至 目标id
        const auto target_fd = connections.find_fd_by_device(received->dst_id);
        // 校验目标id
        if (!target_fd || !connections.find_by_fd(*target_fd)) {
            std::cerr << "route target unavailable: " << device_name(received->dst_id) << '\n';
            continue;
        }
        // 将转发的信息 json序列化
        const auto json = message::serialize_message(*received);
        // 封装
        const auto frame = json ? encode_tcp_frame(*json) : std::nullopt;
        if (!frame) {
            std::cerr << "message error: forwarding encoding failed\n";
            return false;
        }
        // 发送
        if (!queue_tcp_frame(epoll_fd, connections, *target_fd, *frame)) {
            std::cerr << "route target send failed: " << device_name(received->dst_id) << '\n';
            close_client(epoll_fd, connections, *target_fd);
        }
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
        // 校验socket是否成功
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
    const int reuse_address = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) < 0) {
        std::cerr << "setsockopt failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }
    // 绑定服务器socket与ip端口
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 3、开始监听
    // 3. 开始监听
    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 4. 创建 epoll 实例
    const int epoll_fd = epoll_create1(EPOLL_CLOEXEC); // epoll_fd：管理需要监听的 Socket 及其事件
    if (epoll_fd < 0) {
        std::cerr << "epoll_create1 failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    // 5. 将 server_fd 注册到 epoll
    epoll_event server_event{};
    server_event.events = EPOLLIN; // 监听可读事件
    server_event.data.fd = server_fd;
    // epoll_ctl：管理 epoll 的监听列表
    // EPOLL_CTL_ADD：添加 FD 到监听列表
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &server_event) < 0) {
        std::cerr << "epoll_ctl failed: " << std::strerror(errno) << '\n';
        close(epoll_fd);
        close(server_fd);
        return 1;
    }

    // 6. 创建连接管理器
    ConnectionManager connections;
    std::cout << "Gateway listening on 127.0.0.1:5000" << std::endl;
    // 4、接受客户端连接
    // server_fd：负责监听客户端连接
    // client_fd：负责与已经连接的客户端通信
    // 5、接收数据
    epoll_event events[64];
    while (true) {
        // 等待监听FD发生事件(判断谁有没有客户端发消息)
        const int ready = epoll_wait(epoll_fd, events, 64, -1);
        // 返回本次有多少个 FD 发生了事件
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait failed: " << std::strerror(errno) << '\n';
            break;
        }

        // 遍历发生的 FD 事件
        for (int i = 0; i < ready; ++i) {
            const int fd = events[i].data.fd; // 当前事件连接的索引fd
            // 若服务器发出事件，此时fd对应服务器的
            if (fd == server_fd) {
                while (true) {
                    // 开始监听
                    const int client_fd = accept(server_fd, nullptr, nullptr);
                    if (client_fd < 0) { // 监听失败
                        if (errno == EINTR) continue; // EINTR：操作被信号中断，重新执行 accept
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
                        break;
                    }
                    // accept设置为非阻塞
                    if (!set_nonblocking(client_fd)) {
                        std::cerr << "client fcntl failed: " << std::strerror(errno) << '\n';
                        close(client_fd);
                        continue;
                    }
                    // 连接加入 ConnectionManager
                    if (!connections.add_connection(client_fd)) {
                        // 把 accept客户端 FD 添加到 connections_by_fd_ 映射表中
                        std::cerr << "add_connection failed\n";
                        close(client_fd);
                        continue;
                    }
                    // 创建 accept客户端 事件结构体
                    epoll_event client_event{};
                    client_event.events = EPOLLIN | EPOLLRDHUP;
                    client_event.data.fd = client_fd;
                    // 将 accept客户端 加入 epoll
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) < 0) {
                        std::cerr << "client epoll_ctl failed: " << std::strerror(errno) << '\n';
                        connections.remove_connection(client_fd);
                        close(client_fd);
                    }
                }
                continue;
            }
            // 若不是服务器发生事件，而是其余客户但发出
            // 1.查找对应时间对应的连接
            // 1.查找对应fd对应的连接
            Connection* connection = connections.find_by_fd(fd);
            if (!connection) continue;
            bool disconnect = false;
            std::uint8_t buffer[256];
            if (events[i].events & EPOLLIN) while (true) {
                // recv()用于接收数据
                const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
                if (count > 0) { // 接收成功
                    if (!process_data(epoll_fd, connections, fd, *connection, buffer, static_cast<std::size_t>(count))) {
                        disconnect = true;
                        break;
                    }
                } else if (count == 0) { // 接收成功，但客户端断开连接，检查报文完整
                    // 客户端断开连接后，是否存在未处理完的报文数据
                    // 接收异常
                    if (connection->decoder.has_pending_data())
                        std::cerr << "protocol error: incomplete frame at disconnect\n";
                    else
                        std::cout << "Client disconnected\n";
                    disconnect = true;
                    break;
                } else { // 接收失败
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    std::cerr << "recv failed: " << std::strerror(errno) << '\n';
                    disconnect = true;
                    break;
                }
            }
            if (!disconnect && (events[i].events & EPOLLOUT) &&
                !flush_tcp_frames(epoll_fd, *connection, fd)) {
                std::cerr << "send failed: " << std::strerror(errno) << '\n';
                disconnect = true;
            }
            // 判断客户端是否发生异常或断开
            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                if (!disconnect && connection->decoder.has_pending_data())
                    std::cerr << "protocol error: incomplete frame at disconnect\n";
                disconnect = true;
            }
            // 清理客户端连接
            if (disconnect) {
                close_client(epoll_fd, connections, fd);
            }
        }
    }
    close(epoll_fd);
    close(server_fd);
    return 1;
}
