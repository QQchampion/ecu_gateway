#include "gateway/TcpSender.h"
#include "protocol/TcpFrameCodec.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
struct Pair {
    int fd[2];
    int epoll;
    ConnectionManager manager;
    Pair() {
        assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fd) == 0);
        epoll = epoll_create1(0);
        assert(epoll >= 0);
        assert(manager.add_connection(fd[0]));
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = fd[0];
        assert(epoll_ctl(epoll, EPOLL_CTL_ADD, fd[0], &event) == 0);
    }
    ~Pair() { close(fd[0]); close(fd[1]); close(epoll); }
};

void drain(int fd, std::vector<std::uint8_t>& received) {
    std::uint8_t buffer[8192];
    while (true) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) received.insert(received.end(), buffer, buffer + n);
        else if (n < 0 && errno == EINTR) continue;
        else { assert(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)); break; }
    }
}
}

int main() {
    {
        Pair pair;
        auto frame = *encode_tcp_frame("hello");
        assert(queue_tcp_frame(pair.epoll, pair.manager, pair.fd[0], frame));
        assert(pair.manager.find_by_fd(pair.fd[0])->send_queue.empty());
        std::vector<std::uint8_t> received;
        drain(pair.fd[1], received);
        assert(received == frame);
    }
    {
        Pair pair;
        int size = 4096;
        assert(setsockopt(pair.fd[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0);
        std::vector<std::uint8_t> first(100000, 0x31), second(20000, 0x32);
        std::vector<std::uint8_t> expected = first;
        expected.insert(expected.end(), second.begin(), second.end());
        assert(queue_tcp_frame(pair.epoll, pair.manager, pair.fd[0], first));
        Connection* connection = pair.manager.find_by_fd(pair.fd[0]);
        assert(connection && connection->send_offset > 0 && connection->send_offset < first.size());
        const auto offset = connection->send_offset;
        assert(flush_tcp_frames(pair.epoll, *connection, pair.fd[0]));
        assert(connection->send_offset == offset);
        assert(queue_tcp_frame(pair.epoll, pair.manager, pair.fd[0], second));
        assert(connection->send_queue.size() == 2);
        std::vector<std::uint8_t> received;
        for (int i = 0; i < 1000 && !connection->send_queue.empty(); ++i) {
            drain(pair.fd[1], received);
            epoll_event event{};
            assert(epoll_wait(pair.epoll, &event, 1, 1000) == 1);
            assert(event.events & EPOLLOUT);
            assert(flush_tcp_frames(pair.epoll, *connection, pair.fd[0]));
        }
        assert(connection->send_queue.empty() && connection->send_offset == 0);
        drain(pair.fd[1], received);
        assert(received == expected);
        epoll_event event{};
        assert(epoll_wait(pair.epoll, &event, 1, 0) == 0);
    }
    {
        Pair failed, healthy;
        close(failed.fd[1]);
        failed.fd[1] = -1;
        assert(!queue_tcp_frame(failed.epoll, failed.manager, failed.fd[0], {1, 2, 3}));
        auto frame = *encode_tcp_frame("healthy");
        assert(queue_tcp_frame(healthy.epoll, healthy.manager, healthy.fd[0], frame));
        std::vector<std::uint8_t> received;
        drain(healthy.fd[1], received);
        assert(received == frame);
    }
}
