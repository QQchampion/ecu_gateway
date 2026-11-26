#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class TcpFrameDecoder {
public:
    std::optional<std::vector<std::string>> feed(const std::uint8_t* data, std::size_t size);
    void reset();
    bool has_pending_data() const;

private:
    std::vector<std::uint8_t> buffer_;
};
