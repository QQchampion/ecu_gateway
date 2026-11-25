#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

constexpr std::size_t kMaxTcpFrameBodyLength = 4096;
std::optional<std::vector<std::uint8_t>> encode_tcp_frame(const std::string& body);
