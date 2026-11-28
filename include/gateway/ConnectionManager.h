#pragma once

#include "gateway/Connection.h"

#include <optional>
#include <unordered_map>

class ConnectionManager {
public:
    // 添加 TCP 连接
    bool add_connection(int fd);
    // 注册设备身份
    bool register_device(int fd, message::DeviceId id);
    // 通过 fd 找对应连接 (fd：文件描述符)
    Connection* find_by_fd(int fd);
    // 通过 设备id 找 fd
    std::optional<int> find_fd_by_device(message::DeviceId id) const;
    // 删除连接
    bool remove_connection(int fd);

private:
    // 保存所有 fd 与 连接 的键值
    std::unordered_map<int, Connection> connections_by_fd_;
    // 保存所有 设备id 和 fd 的键值
    std::unordered_map<message::DeviceId, int> fd_by_device_;
};
