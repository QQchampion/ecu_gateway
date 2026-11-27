#pragma once

#include "gateway/ConnectionManager.h"

#include <cstdint>
#include <vector>

// Returns false only when this client must be disconnected.
bool queue_tcp_frame(int epoll_fd, ConnectionManager& connections, int fd,
                     std::vector<std::uint8_t> frame);
bool flush_tcp_frames(int epoll_fd, Connection& connection, int fd);
