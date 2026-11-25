# ECU 通信网关

基于 C++17 的 Linux ECU 通信网关练习项目。当前实现了 Gateway 与 Power ECU 模拟程序之间的一次 TCP 请求与响应，以及独立的长度前缀帧编解码模块。项目面向通信协议和网络编程的逐步实践。

## 当前功能

- Gateway 在 `127.0.0.1:5000` 上运行阻塞式 TCP Server，接受一个客户端。
- Power ECU 作为阻塞式 TCP Client，发送车速正文 `speed=60`。
- Gateway 解析车速帧并返回 `ACK: received`；Power ECU 解析并校验 ACK，然后双方关闭连接。
- `TcpFrameCodec` 封装帧；`TcpFrameDecoder` 增量解析字节流，处理长度前缀或正文分段到达，以及一次接收包含多帧的情况。
- 两个独立测试程序验证编码和解码逻辑。

## 目录结构

```text
apps/gateway/main.cpp             Gateway TCP Server 入口
apps/power_ecu/main.cpp           Power ECU TCP Client 入口
include/protocol/                 协议模块公共头文件
src/protocol/                     帧编码与增量解码实现
tests/                            协议测试程序
docs/                             文档目录
CMakeLists.txt                    构建配置
```

## 技术栈与通信架构

项目使用 Ubuntu 24.04 / Linux、C++17、POSIX Socket API、CMake 和 Ninja，不依赖第三方库。`protocol` 是静态库，供 Gateway、Power ECU 和测试程序链接。

Power ECU 将正文编码为传输帧，通过 TCP 发送给 Gateway；Gateway 用每次 `recv()` 的原始字节增量解码，收到 `speed=60` 后编码并发送 ACK。Power ECU 同样增量解码 ACK。当前使用单个阻塞式连接，完成一次请求与响应后退出。

## 长度前缀协议

帧格式为 `[4 字节正文长度（大端）][正文原始字节]`。长度仅计算正文，最大正文长度为 4096 字节。`speed=60` 的正文为 8 字节，示意如下（空格仅用于阅读，网络传输中没有分隔空格）：

```text
00 00 00 08 speed=60
```

`TcpFrameCodec` 负责生成完整帧，超长正文返回失败。`TcpFrameDecoder` 缓存不完整数据，提取一条或多条完整正文；长度字段超过上限时返回协议错误并清空缓存。消息边界由长度字段决定，不依赖换行符。

## 编译与测试

在项目根目录运行（需要已安装 CMake、Ninja 和 C++17 编译器）：

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/tcp_frame_codec_test
./build/tcp_frame_decoder_test
```

## 运行

先在终端一启动 Gateway：

```bash
./build/gateway
```

再在终端二启动 Power ECU：

```bash
./build/power_ecu
```

预期 Gateway 输出 `Received: speed=60`，Power ECU 输出 `Connected to Gateway` 和 `ACK: received`。两个程序在本次通信结束后退出，再次运行需要先重新启动 Gateway。由于绑定回环地址 `127.0.0.1`，两端需在同一主机上运行。

## 开发进度

已完成工程拆分、TCP 单次双向通信、4 字节大端长度前缀编解码及协议测试。后续可继续完善协议边界测试和错误场景验证，再逐步扩展业务报文；JSON、UART、多设备并发和自动重连目前尚未实现。
