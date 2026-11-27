#include "gateway/TcpSender.h"

#include <cerrno>
#include <utility>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace {

// 更新 EPOLLOUT(可写事件)监听状态
bool update_write_interest(int epoll_fd, int fd, bool enabled)
{
    epoll_event event{};
    // 使 Socket 具有能力：监听可读、写入方向、发送数据
    event.events = EPOLLIN | EPOLLRDHUP | (enabled ? EPOLLOUT : 0);
    event.data.fd = fd;
    // 修改已经登记的 Socket 的监听事件
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == 0;
}
}

// 发送队列中的数据
bool flush_tcp_frames(int epoll_fd, Connection& connection, int fd)
{
    while (!connection.send_queue.empty()) { // 发送队列不为空
        const auto& frame = connection.send_queue.front(); // 队首报文
        // 发送的报文字节未完成
        while (connection.send_offset < frame.size()) {
            const ssize_t sent = send(fd, frame.data() + connection.send_offset,
                                      frame.size() - connection.send_offset, MSG_NOSIGNAL);
            if (sent > 0) { // 发送成功
                connection.send_offset += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent == 0) return false; // 发送失败
            if (errno == EINTR) continue; // 系统调用信号被中断
            if (errno == EAGAIN || errno == EWOULDBLOCK)  // Socket 当前暂时无法完成发送操作，需要稍后重试
            // EAGAIN：需要再次尝试；EWOULDBLOCK：操作将会阻塞
                // 将 EPOLLOUT 开启
                return update_write_interest(epoll_fd, fd, true);
            return false;
        }
        connection.send_queue.pop_front();
        connection.send_offset = 0;
    }
    // 将EPOLLOUT关闭
    return update_write_interest(epoll_fd, fd, false);
}

// 发送入口
bool queue_tcp_frame(int epoll_fd, ConnectionManager& connections, int fd,
                     std::vector<std::uint8_t> frame)
{
    Connection* connection = connections.find_by_fd(fd);
    // 校验连接和帧合法性
    if (!connection || frame.empty()) return false;
    connection->send_queue.push_back(std::move(frame));
    return flush_tcp_frames(epoll_fd, *connection, fd);
}
