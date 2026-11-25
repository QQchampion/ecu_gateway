#include "protocol/TcpFrameCodec.h"
#include <iostream>
#include <string>

int main()
{
    const std::string body = "speed=60";
    const auto frame = encode_tcp_frame(body);
    if (!frame || frame->size() != 12 || (*frame)[0] != 0 || (*frame)[1] != 0 ||
        (*frame)[2] != 0 || (*frame)[3] != 8 ||
        std::string(frame->begin() + 4, frame->end()) != body) {
        std::cerr << "FAIL: speed=60 frame\n";
        return 1;
    }
    std::cout << "PASS: speed=60 -> 00 00 00 08 + speed=60\n";
    if (encode_tcp_frame(std::string(kMaxTcpFrameBodyLength + 1, 'x'))) {
        std::cerr << "FAIL: oversized body accepted\n";
        return 1;
    }
    std::cout << "PASS: body over 4096 bytes rejected\n";
    return 0;
}
