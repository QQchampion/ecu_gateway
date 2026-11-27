#pragma once

#include "message/MessageTypes.h"
#include "protocol/TcpFrameDecoder.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

struct Connection {

    // 接收数据：解析tcp数据
    TcpFrameDecoder decoder;
    // 设备身份，报文的发送方
    std::optional<message::DeviceId> device_id;
    // 发送队列：保存发送tcp帧
    std::deque<std::vector<std::uint8_t>> send_queue;
    // 发送偏移量：当前报文已发送字节
    std::size_t send_offset{0};
};
