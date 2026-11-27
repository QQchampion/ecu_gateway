#include "gateway/ConnectionManager.h"

namespace {
// 判断tcp是否允许接入
bool is_tcp_device(message::DeviceId id)
{
    switch (id) {
        case message::DeviceId::POWER_ECU:
        case message::DeviceId::CLUSTER_ECU:
        case message::DeviceId::EXTERNAL_CLIENT:
            return true;
        case message::DeviceId::GATEWAY:
        case message::DeviceId::BCM_ECU:
            return false;
    }
    return false;
}
}  // namespace

//  创建连接
bool ConnectionManager::add_connection(int fd)
{
    if (fd < 0) return false;
    return connections_by_fd_.try_emplace(fd).second;
    // try_emplace：向映射表中插入元素，second：表示是否成功
}

// 登记设备身份
bool ConnectionManager::register_device(int fd, message::DeviceId id)
{
    auto connection = connections_by_fd_.find(fd);
    // 校验是否登记过
    if (connection == connections_by_fd_.end() || connection->second.device_id ||
        !is_tcp_device(id) || fd_by_device_.find(id) != fd_by_device_.end()) {
        // end()表示没找到
        return false;
    }
    // 建立fd和设备id联系
    fd_by_device_.emplace(id, fd); // emplace:向容器中插入一个新元素
    // 更新连接链路的设备id (设置fd的连接对象的设备id)
    connection->second.device_id = id;
    return true;
}

// 通过 键(fd) 查找对应 值(连接)
Connection* ConnectionManager::find_by_fd(int fd)
{
    auto connection = connections_by_fd_.find(fd);
    // 校验连接是否存在
    return connection == connections_by_fd_.end() ? nullptr : &connection->second;
}

// 通过 键(设备) 查找对应 值(fd)
std::optional<int> ConnectionManager::find_fd_by_device(message::DeviceId id) const
{
    auto device = fd_by_device_.find(id);
    if (device == fd_by_device_.end()) return std::nullopt;
    return device->second;
}

// 删除对应的连接
bool ConnectionManager::remove_connection(int fd)
{
    auto connection = connections_by_fd_.find(fd);
    // 检查删除的连接是否存在
    if (connection == connections_by_fd_.end()) return false;
    // 删除设备索引
    if (connection->second.device_id) {
        auto device = fd_by_device_.find(*connection->second.device_id);
        if (device != fd_by_device_.end() && device->second == fd) {
            fd_by_device_.erase(device);
        }
    }
    // 删除 Connection
    connections_by_fd_.erase(connection);
    return true;
}
