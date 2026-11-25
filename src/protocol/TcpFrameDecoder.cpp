#include "protocol/TcpFrameDecoder.h"
#include "protocol/TcpFrameCodec.h"

std::optional<std::vector<std::string>> TcpFrameDecoder::feed(const std::uint8_t* data, std::size_t size)
{
    // 校验参数和大小
    if (data == nullptr && size != 0) return std::nullopt; // nullptr：空指针，

    // 1、将数据存入缓冲区
    if (size != 0) buffer_.insert(buffer_.end(), data, data + size);

    std::vector<std::string> messages;
    std::size_t offset = 0; // offset：已经处理了多少字节，下一条解析报文的起始位置
    //2、判断头帧是否完全接收，报文正文长度
    while (buffer_.size() - offset >= 4) {
        // 3、获取报文正文长度
        const std::uint32_t length =
            (static_cast<std::uint32_t>(buffer_[offset]) << 24) |
            (static_cast<std::uint32_t>(buffer_[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(buffer_[offset + 2]) << 8) |
            static_cast<std::uint32_t>(buffer_[offset + 3]);
        if (length > kMaxTcpFrameBodyLength) { // 校验
            reset();
            return std::nullopt;
        }
        // 4、判断报文是否完整，不完整则等待
        if (buffer_.size() - offset < 4 + length) break; // ！offset是上一条处理报文，length是本次处理报文

        // 5、提取正文
        // begin(): 起始位置 + 本次打算处理报文的正文
        const auto begin = buffer_.begin() + static_cast<std::ptrdiff_t>(offset + 4);
        messages.emplace_back(begin, begin + length);// emplace_back将构造元素放入末尾
        offset += 4 + length; // 本条报文处理完毕，移动至下一条报文
    }
    // 清理缓存中处理过的报文
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    return messages;
}

void TcpFrameDecoder::reset()
{
    buffer_.clear();
}
