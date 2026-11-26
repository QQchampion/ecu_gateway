#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace message {

// 设备编号
enum class DeviceId : std::uint8_t {
    POWER_ECU = 0x01,
    BCM_ECU = 0x02,
    CLUSTER_ECU = 0x03, // 仪表ECU
    GATEWAY = 0x04,
    EXTERNAL_CLIENT = 0x05
};

// 业务消息类型
enum class MessageType : std::uint8_t {
    STATUS = 0x01,
    CONTROL_REQ = 0x02, // 控制请求
    CONTROL_RESP = 0x03, // 控制响应
    HEARTBEAT = 0x04 // 心跳
};

// 发动机状态
enum class EngineStatus { STOPPED, STARTING, RUNNING, FAULT };
// 车灯类型、状态、控制命令、控制结果
enum class LightType { LOW_BEAM, HIGH_BEAM, POSITION };
enum class LightState { ON, OFF };
enum class ControlCommand { SET_LIGHT };
enum class ControlResult { SUCCESS, FAILED };

// 错误码
enum class ErrorCode : std::uint16_t {
    SUCCESS = 0,
    INVALID_PARAMETER = 1001, //参数错误
    UNSUPPORTED_COMMAND = 1002, //不支持的命令
    UNSUPPORTED_LIGHT = 1003, //不支持的车灯
    EXECUTION_FAILED = 1004
};

// 车辆状态上报
struct StatusPayload {
    double speed_kmh{};
    std::uint32_t rpm{};
    double coolant_temp_c{};
    EngineStatus engine_status{};
};

// 控制命令
struct ControlReqPayload {
    ControlCommand command{};
    LightType light{};
    LightState state{};
};

// 控制响应
struct ControlRespPayload {
    ControlResult result{};
    ErrorCode error_code{};
    std::string message;
};

// 心跳
struct HeartbeatPayload {};

// 可保存四种消息类型的同一类型
using Payload = std::variant<StatusPayload, ControlReqPayload,
                             ControlRespPayload, HeartbeatPayload>;

// 统一业务消息结构
struct Message {
    DeviceId src_id{};
    DeviceId dst_id{};
    MessageType msg_type{};
    std::uint32_t seq{};
    Payload payload;
};

}  // namespace message
