#include "message/MessageJsonCodec.h"
#include "protocol/TcpFrameCodec.h"
#include "protocol/TcpFrameDecoder.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <variant>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
    // 1. 创建客户端fd
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;

    // 2. 绑定ip和端口
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        return 1;
    }

    // 3. 创建心跳信息并发送
    const message::Message heartbeat{message::DeviceId::CLUSTER_ECU,
        message::DeviceId::GATEWAY, message::MessageType::HEARTBEAT, 1,
        message::HeartbeatPayload{}};
    // 序列化json
    const auto json = message::serialize_message(heartbeat);
    // tcp封装
    const auto frame = json ? encode_tcp_frame(*json) : std::nullopt;
    if (!frame) { close(fd); return 1; }
    // 按字节发送
    std::size_t offset = 0;
    while (offset < frame->size()) {
        const ssize_t count = send(fd, frame->data() + offset, frame->size() - offset, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return 1; }
        offset += static_cast<std::size_t>(count);
    }
    std::cout << "Cluster ECU heartbeat sent" << std::endl;

    // 5. 接收信息
    TcpFrameDecoder decoder;
    std::uint8_t buffer[256];
    while (true) {
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { // 校验接收是否成功
            const bool complete = !decoder.has_pending_data();
            if (count < 0 || !complete) std::cerr << "receive failed or incomplete frame\n";
            close(fd);
            return count == 0 && complete ? 0 : 1;
        }
        // 解析报文
        const auto bodies = decoder.feed(buffer, static_cast<std::size_t>(count));
        if (!bodies) { std::cerr << "invalid TCP frame\n"; close(fd); return 1; }
        for (const auto& body : *bodies) {
            const auto received = message::deserialize_message(body);
            if (!received || received->dst_id != message::DeviceId::CLUSTER_ECU ||
                received->msg_type != message::MessageType::STATUS ||
                !std::holds_alternative<message::StatusPayload>(received->payload)) {
                std::cerr << "invalid STATUS message\n";
                close(fd);
                return 1;
            }
            // 打印接收的消息
            const auto& status = std::get<message::StatusPayload>(received->payload);
            std::cout << "STATUS seq=" << received->seq << " speed_kmh=" << status.speed_kmh
                      << " rpm=" << status.rpm << " coolant_temp_c=" << status.coolant_temp_c << std::endl;
        }
    }
}
