#include "message/MessageJsonCodec.h"

#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <utility>

namespace message {
namespace {
using json = nlohmann::json;

template <typename E>
std::optional<E> enum_value(const json& value, std::initializer_list<std::pair<const char*, E>> entries) {
    if (!value.is_string()) return std::nullopt;
    const auto text = value.get<std::string>();
    for (const auto& entry : entries) if (text == entry.first) return entry.second;
    return std::nullopt;
}
template <typename E>
std::optional<const char*> enum_name(E value, std::initializer_list<std::pair<const char*, E>> entries) {
    for (const auto& entry : entries) if (value == entry.second) return entry.first;
    return std::nullopt;
}
#define ENGINE {{ "STOPPED", EngineStatus::STOPPED }, { "STARTING", EngineStatus::STARTING }, { "RUNNING", EngineStatus::RUNNING }, { "FAULT", EngineStatus::FAULT }}
#define LIGHT {{ "LOW_BEAM", LightType::LOW_BEAM }, { "HIGH_BEAM", LightType::HIGH_BEAM }, { "POSITION", LightType::POSITION }}
#define STATE {{ "ON", LightState::ON }, { "OFF", LightState::OFF }}
#define COMMAND {{ "SET_LIGHT", ControlCommand::SET_LIGHT }}
#define RESULT {{ "SUCCESS", ControlResult::SUCCESS }, { "FAILED", ControlResult::FAILED }}

// 数据校验辅助函数
// 检查 JSON 中的数据是否合法，合法才允许转换
std::optional<std::uint32_t> u32(const json& value) {
    if (!value.is_number_integer()) return std::nullopt; // 判空
    // 判断无符号整数
    if (value.is_number_unsigned()) {
        // 从 JSON 对象中取出数值，并按照 uint64_t 类型读取
        const auto n = value.get<std::uint64_t>();
        if (n <= std::numeric_limits<std::uint32_t>::max()) return static_cast<std::uint32_t>(n);
    } else {
        const auto n = value.get<std::int64_t>();
        if (n >= 0 && n <= std::numeric_limits<std::uint32_t>::max()) return static_cast<std::uint32_t>(n);
    }
    return std::nullopt;
}
std::optional<DeviceId> device(const json& value) {
    const auto n = u32(value);
    if (!n || *n < 1 || *n > 5) return std::nullopt;
    return static_cast<DeviceId>(*n);
}
std::optional<MessageType> type(const json& value) {
    const auto n = u32(value);
    if (!n || *n < 1 || *n > 4) return std::nullopt;
    return static_cast<MessageType>(*n);
}
std::optional<ErrorCode> error(const json& value) {
    const auto n = u32(value);
    if (!n) return std::nullopt;
    switch (*n) {
        case 0: case 1001: case 1002: case 1003: case 1004:
            return static_cast<ErrorCode>(*n);
        default: return std::nullopt;
    }
}
std::optional<double> number(const json& value) {
    if (!value.is_number()) return std::nullopt;
    const double n = value.get<double>();
    if (!std::isfinite(n)) return std::nullopt;
    return n;
}
// 判断消息类型和实际数据是否匹配
bool matches(const Message& m) {
    switch (m.msg_type) {
        case MessageType::STATUS: return std::holds_alternative<StatusPayload>(m.payload);
        case MessageType::CONTROL_REQ: return std::holds_alternative<ControlReqPayload>(m.payload);
        case MessageType::CONTROL_RESP: return std::holds_alternative<ControlRespPayload>(m.payload);
        case MessageType::HEARTBEAT: return std::holds_alternative<HeartbeatPayload>(m.payload);
    }
    return false;
}
}  // namespace

// 序列化：消息对象->JSON
std::optional<std::string> serialize_message(const Message& m) {
    try {
        // 校验消息合法
        if (!device(static_cast<int>(m.src_id)) || !device(static_cast<int>(m.dst_id)) ||
            !type(static_cast<int>(m.msg_type)) || m.seq == 0 || !matches(m)) return std::nullopt;
        json payload = json::object(); // 创建json对象
        // 根据消息类型，序列化不同的 payload
        switch (m.msg_type) {
            case MessageType::STATUS: {
                const auto& p = std::get<StatusPayload>(m.payload);
                const auto engine = enum_name(p.engine_status, ENGINE);
                if (!engine || !std::isfinite(p.speed_kmh) || !std::isfinite(p.coolant_temp_c)) return std::nullopt;
                payload = {{"speed_kmh", p.speed_kmh}, {"rpm", p.rpm},
                           {"coolant_temp_c", p.coolant_temp_c}, {"engine_status", *engine}};
                break;
            }
            case MessageType::CONTROL_REQ: {
                const auto& p = std::get<ControlReqPayload>(m.payload);
                const auto command = enum_name(p.command, COMMAND);
                const auto light = enum_name(p.light, LIGHT);
                const auto state = enum_name(p.state, STATE);
                if (!command || !light || !state) return std::nullopt;
                payload = {{"command", *command}, {"light", *light}, {"state", *state}};
                break;
            }
            case MessageType::CONTROL_RESP: {
                const auto& p = std::get<ControlRespPayload>(m.payload);
                const auto result = enum_name(p.result, RESULT);
                if (!result || !error(static_cast<int>(p.error_code))) return std::nullopt;
                payload = {{"result", *result}, {"error_code", static_cast<int>(p.error_code)},
                           {"message", p.message}};
                break;
            }
            case MessageType::HEARTBEAT: break;
        }
        return json{{"src_id", static_cast<int>(m.src_id)}, {"dst_id", static_cast<int>(m.dst_id)},
                    {"msg_type", static_cast<int>(m.msg_type)}, {"seq", m.seq},
                    {"payload", payload}}.dump();
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

// 反序列化：JSON->消息对象
std::optional<Message> deserialize_message(const std::string& text) {
    try {
        const json j = json::parse(text);
        // 校验 JSON 对象的结构和字段
        if (!j.is_object() || !j.contains("src_id") || !j.contains("dst_id") ||
            !j.contains("msg_type") || !j.contains("seq") || !j.contains("payload")) return std::nullopt;
        const auto src = device(j.at("src_id"));
        const auto dst = device(j.at("dst_id"));
        const auto kind = type(j.at("msg_type"));
        const auto seq = u32(j.at("seq"));
        const auto& p = j.at("payload");
        if (!src || !dst || !kind || !seq || *seq == 0 || !p.is_object()) return std::nullopt;
        Payload payload;
        // 判断消息类型
        switch (*kind) {
            case MessageType::STATUS: {
                if (!p.contains("speed_kmh") || !p.contains("rpm") ||
                    !p.contains("coolant_temp_c") || !p.contains("engine_status")) return std::nullopt;
                const auto speed = number(p.at("speed_kmh"));
                const auto rpm = u32(p.at("rpm"));
                const auto temp = number(p.at("coolant_temp_c"));
                const auto engine = enum_value<EngineStatus>(p.at("engine_status"), ENGINE);
                if (!speed || !rpm || !temp || !engine) return std::nullopt;
                payload = StatusPayload{*speed, *rpm, *temp, *engine};
                break;
            }
            case MessageType::CONTROL_REQ: {
                if (!p.contains("command") || !p.contains("light") || !p.contains("state")) return std::nullopt;
                const auto command = enum_value<ControlCommand>(p.at("command"), COMMAND);
                const auto light = enum_value<LightType>(p.at("light"), LIGHT);
                const auto state = enum_value<LightState>(p.at("state"), STATE);
                if (!command || !light || !state) return std::nullopt;
                payload = ControlReqPayload{*command, *light, *state};
                break;
            }
            case MessageType::CONTROL_RESP: {
                if (!p.contains("result") || !p.contains("error_code") ||
                    !p.contains("message") || !p.at("message").is_string()) return std::nullopt;
                const auto result = enum_value<ControlResult>(p.at("result"), RESULT);
                const auto code = error(p.at("error_code"));
                if (!result || !code) return std::nullopt;
                payload = ControlRespPayload{*result, *code, p.at("message").get<std::string>()};
                break;
            }
            case MessageType::HEARTBEAT:
                if (!p.empty()) return std::nullopt;
                payload = HeartbeatPayload{};
                break;
        }
        return Message{*src, *dst, *kind, *seq, std::move(payload)};
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

}  // namespace message
