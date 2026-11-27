#pragma once

#include "gateway/Connection.h"

#include <optional>
#include <unordered_map>

class ConnectionManager {
public:
    bool add_connection(int fd);
    bool register_device(int fd, message::DeviceId id);
    Connection* find_by_fd(int fd);
    std::optional<int> find_fd_by_device(message::DeviceId id) const;
    bool remove_connection(int fd);

private:
    std::unordered_map<int, Connection> connections_by_fd_;
    std::unordered_map<message::DeviceId, int> fd_by_device_;
};
