#include "protocol/TcpFrameCodec.h"
#include "protocol/TcpFrameDecoder.h"

#include <iostream>
#include <string>
#include <vector>

int main()
{
    const auto first = *encode_tcp_frame("speed=60");
    const auto second = *encode_tcp_frame("rpm=3000");
    TcpFrameDecoder decoder;

    auto result = decoder.feed(first.data(), first.size());
    if (!result || *result != std::vector<std::string>{"speed=60"}) return 1;
    std::cout << "PASS: complete frame\n";

    result = decoder.feed(first.data(), 2);
    if (!result || !result->empty()) return 1;
    result = decoder.feed(first.data() + 2, first.size() - 2);
    if (!result || *result != std::vector<std::string>{"speed=60"}) return 1;
    std::cout << "PASS: split length prefix\n";

    result = decoder.feed(first.data(), 6);
    if (!result || !result->empty()) return 1;
    result = decoder.feed(first.data() + 6, first.size() - 6);
    if (!result || *result != std::vector<std::string>{"speed=60"}) return 1;
    std::cout << "PASS: split body\n";

    auto combined = first;
    combined.insert(combined.end(), second.begin(), second.end());
    result = decoder.feed(combined.data(), combined.size());
    if (!result || *result != std::vector<std::string>{"speed=60", "rpm=3000"}) return 1;
    std::cout << "PASS: two frames in one feed\n";

    const std::uint8_t invalid[] = {0, 0, 0x10, 0x01};
    if (decoder.feed(invalid, sizeof(invalid))) return 1;
    result = decoder.feed(first.data(), first.size());
    if (!result || *result != std::vector<std::string>{"speed=60"}) return 1;
    std::cout << "PASS: 4097-byte length rejected and state cleared\n";

    result = decoder.feed(first.data(), 2);
    if (!result || !result->empty()) return 1;
    decoder.reset();
    result = decoder.feed(second.data(), second.size());
    if (!result || *result != std::vector<std::string>{"rpm=3000"}) return 1;
    std::cout << "PASS: reset clears partial frame\n";

    if (decoder.feed(nullptr, 1)) return 1;
    std::cout << "PASS: null pointer with nonzero size rejected\n";
    return 0;
}
