# M3-08-01-04：ConnectionManager 连接管理器设计

## 1. 设计目标

在 Gateway 中管理多个客户端连接，实现：

- 根据 Socket FD 查找对应的 Connection。
- 根据 DeviceId 查找对应的 Socket FD。
- 完成设备身份登记。
- 客户端断开时，清理连接及相关映射。

ConnectionManager 是 Gateway 的连接管理模块，为后续 epoll 事件处理和 MessageRouter 消息路由提供基础支持。

## 2. 整体数据结构

采用两个 `std::unordered_map`，建立两种不同方向的映射关系。

### 2.1 核心连接表

```cpp
std::unordered_map<int, Connection> connections_by_fd_;
```

映射关系：

`Socket FD → Connection`

作用：根据 Socket 文件描述符，快速找到对应的连接对象。

示例：

| Socket FD | Connection |
|---|---|
| 5 | Power ECU 连接对象 |
| 6 | Cluster ECU 连接对象 |
| 7 | 尚未登记身份的连接对象 |

主要使用场景：

epoll_wait() 返回一个就绪 FD 后，Gateway 根据 FD 找到对应 Connection，进而使用该连接独立的 TcpFrameDecoder 接收和解析数据。

### 2.2 设备索引表

```cpp
std::unordered_map<message::DeviceId, int> fd_by_device_;
```

映射关系：

`DeviceId → Socket FD`

作用：根据业务消息中的目标设备 ID，快速找到对应的 Socket 文件描述符。

示例：

| DeviceId | Socket FD |
|---|---|
| POWER_ECU | 5 |
| CLUSTER_ECU | 6 |

主要使用场景：

Gateway 收到一条业务消息后，根据 dst_id 找到目标设备的 FD，再通过核心连接表找到目标设备的 Connection，完成后续消息转发。

### 2.3 两个映射表的关系

```text
DeviceId
   |
   v
fd_by_device_
   |
   v
Socket FD
   |
   v
connections_by_fd_
   |
   v
Connection
```

两个映射表管理的是同一批连接，只是提供不同方向的查找入口。

其中，connections_by_fd_ 是核心连接表，fd_by_device_ 是辅助索引表。

## 3. ConnectionManager 类设计

### 3.1 成员变量

```cpp
private:
    std::unordered_map<int, Connection> connections_by_fd_;
    std::unordered_map<message::DeviceId, int> fd_by_device_;
```

两个映射表均设置为 private，外部不能直接修改，必须通过 ConnectionManager 提供的接口进行管理。

### 3.2 核心接口

| 接口 | 返回值 | 主要职责 |
|---|---|---|
| add_connection(int fd) | bool | 添加新客户端连接 |
| register_device(int fd, DeviceId id) | bool | 登记设备身份 |
| find_by_fd(int fd) | Connection* | 根据 FD 查找连接 |
| find_fd_by_device(DeviceId id) | optional<int> | 根据设备 ID 查找 FD |
| remove_connection(int fd) | bool | 删除连接及其设备索引 |

## 4. 五个核心接口的执行逻辑

### 4.1 add_connection(int fd)

新客户端通过 accept() 建立连接后，Gateway 获得一个新的 Socket FD。

执行逻辑：

1. 检查 FD 是否已经存在。
2. 创建一个新的 Connection 对象。
3. 将 FD 和 Connection 放入 connections_by_fd_。
4. 返回操作结果。

此时设备身份尚未登记，Connection 中的 device_id 为 std::nullopt。

### 4.2 register_device(int fd, DeviceId id)

设备建立连接后，必须首先发送合法的 HEARTBEAT 消息。

Gateway 校验消息后，调用 register_device() 登记身份。

执行逻辑：

1. 检查 FD 是否存在。
2. 检查该连接是否已经登记过设备身份。
3. 检查该 DeviceId 是否已经被其他连接占用。
4. 将 id 保存到对应 Connection 的 device_id。
5. 在 fd_by_device_ 中建立 DeviceId → FD 映射。
6. 返回操作结果。

重复登记或同一设备重复连接时，拒绝登记，不覆盖已有有效连接。

注意：HEARTBEAT 消息内容的合法性、src_id 与连接身份的匹配校验，由上层消息处理逻辑负责；ConnectionManager 只负责维护连接与设备身份映射。

### 4.3 find_by_fd(int fd)

根据 Socket FD 查找 Connection。

执行逻辑：

1. 在 connections_by_fd_ 中调用 find(fd)。
2. 找到则返回对应 Connection 对象的指针。
3. 未找到则返回 nullptr。

主要用于 epoll 事件处理。

### 4.4 find_fd_by_device(DeviceId id)

根据设备 ID 查找对应 Socket FD。

执行逻辑：

1. 在 fd_by_device_ 中调用 find(id)。
2. 找到则返回对应 FD。
3. 未找到则返回 std::nullopt。

主要用于消息路由。

### 4.5 remove_connection(int fd)

客户端断开连接时，清理对应连接。

执行逻辑：

1. 根据 FD 查找 Connection。
2. 如果 Connection 已经登记设备身份，则找到对应 DeviceId。
3. 删除 fd_by_device_ 中指向该 FD 的设备映射。
4. 删除 connections_by_fd_ 中对应的 Connection。
5. 返回操作结果。

如果 FD 不存在，返回 false。

注意：ConnectionManager 只负责管理数据结构和映射关系，不负责调用 close()、epoll_ctl() 等系统接口。Socket 关闭和 epoll 事件注销由 Gateway 网络事件循环负责。

## 5. 业务执行流程

### 5.1 新设备接入

```text
ECU 建立 TCP 连接
        |
        v
Gateway accept()
        |
        v
获得 client_fd
        |
        v
add_connection(client_fd)
        |
        v
等待首次 HEARTBEAT
        |
        v
校验消息合法性
        |
        v
register_device(client_fd, src_id)
        |
        v
完成设备身份登记
```

### 5.2 正常接收数据

```text
epoll_wait() 返回就绪事件
        |
        v
获得 Socket FD
        |
        v
find_by_fd(fd)
        |
        v
找到对应 Connection
        |
        v
使用该连接独立的 TcpFrameDecoder
        |
        v
提取完整 JSON 正文
```

### 5.3 消息转发

假设 Power ECU 需要向 Cluster ECU 发送 STATUS 消息。

```text
Gateway 收到 STATUS 消息
        |
        v
读取 dst_id = CLUSTER_ECU
        |
        v
find_fd_by_device(CLUSTER_ECU)
        |
        v
获得 Cluster ECU 对应 FD
        |
        v
find_by_fd(fd)
        |
        v
找到目标 Connection
        |
        v
将待发送 TCP 帧放入 send_queue
```

后续由 Gateway 的非阻塞发送逻辑负责实际发送。

### 5.4 客户端断开

```text
检测到客户端断开
        |
        v
remove_connection(fd)
        |
        v
清理设备 ID 映射
        |
        v
清理 Connection 对象
        |
        v
Gateway 事件循环关闭 Socket
```

## 6. 模块职责划分

| 模块 | 职责 |
|---|---|
| Connection | 保存单个客户端的通信状态 |
| ConnectionManager | 管理 Connection 和设备身份映射 |
| DeviceManager | 管理设备 ONLINE、DEGRADED、OFFLINE 状态及心跳 |
| MessageValidator | 检查业务消息是否合法 |
| MessageRouter | 根据 dst_id 确定目标设备并执行消息路由 |
| Gateway 事件循环 | 管理 Socket、epoll 事件和实际网络收发 |

ConnectionManager 不负责设备心跳超时判断，也不负责实际消息转发。

## 7. 设计约束

1. 一个有效 Socket FD 对应一个 Connection。
2. 一个已登记的 DeviceId 同一时间只能对应一个有效 FD。
3. 一个 Connection 同一时间只能登记一个 DeviceId。
4. 尚未登记身份的连接，其 device_id 为 std::nullopt。
5. 删除连接时，必须同步清理两个映射表。
6. 两个映射表只能通过 ConnectionManager 的接口修改。
7. 当前采用单线程 epoll 事件循环设计，暂不引入线程锁。
8. ConnectionManager 不负责 Socket 的创建、关闭及 epoll 注册。

## 8. 测试要求

后续独立测试至少覆盖：

- 新增连接成功。
- 重复 FD 添加失败。
- 根据 FD 查找连接成功。
- 不存在的 FD 返回 nullptr。
- 正常设备身份登记成功。
- 不存在的 FD 无法登记。
- 同一连接重复登记失败。
- 相同 DeviceId 被其他连接重复登记时失败。
- 根据 DeviceId 查找 FD 成功。
- 不存在的 DeviceId 返回 std::nullopt。
- 删除已登记连接后，两个映射表均完成清理。
- 删除未登记连接时，能够正确清理核心连接表。

## 9. 本阶段范围

本阶段只实现 ConnectionManager 的数据管理和接口。

暂不实现：

- epoll 事件循环。
- 多客户端实际网络通信。
- HEARTBEAT 接收和处理。
- DeviceManager。
- MessageValidator。
- MessageRouter。
- UART 通信。

以上功能在后续阶段逐步实现。

---

**阶段目标：** 建立 Gateway 的连接管理基础，使系统能够根据 Socket FD 和 DeviceId 两种不同入口，快速定位对应的 Connection，为后续多设备通信和消息路由提供支持。