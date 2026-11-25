#include "protocol/TcpFrameCodec.h"

std::optional<std::vector<std::uint8_t>> encode_tcp_frame(const std::string& body)
{
    // 1、校正正文长度
    if (body.size() > kMaxTcpFrameBodyLength) return std::nullopt;
    // 2、获取正文长度
    const auto length = static_cast<std::uint32_t>(body.size());
    // 3、准备将报文组装成帧
    std::vector<std::uint8_t> frame;
    frame.reserve(4 + body.size()); // 提前分配足够内存
    // 4、添加 4 字节长度前缀
    frame.push_back(static_cast<std::uint8_t>(length >> 24)); // 获取第一高位字节
    frame.push_back(static_cast<std::uint8_t>(length >> 16)); // 获取第二高位字节
    frame.push_back(static_cast<std::uint8_t>(length >> 8));
    frame.push_back(static_cast<std::uint8_t>(length));
    // 5、添加正文，将body插入到帧头
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}
