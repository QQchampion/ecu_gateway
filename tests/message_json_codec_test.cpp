#include "message/MessageJsonCodec.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <variant>

using namespace message;
using nlohmann::json;

// 传入一个 Message，把它转换成 JSON，再把 JSON 转换回 Message，最后检查转换前后是否一致。
void roundtrip(const Message& original) {
    // 序列化：消息对象->JSON
    const auto encoded = serialize_message(original);
    assert(encoded); // 断言：负责检查转换结果是否有效

    // 反序列化：JSON->消息对象
    const auto restored = deserialize_message(*encoded);
    assert(restored);
    assert(restored->src_id == original.src_id);
    assert(restored->dst_id == original.dst_id);
    assert(restored->msg_type == original.msg_type);
    assert(restored->seq == original.seq);
    assert(restored->payload.index() == original.payload.index());
    switch (original.msg_type) {
        case MessageType::STATUS: {
            const auto& expected = std::get<StatusPayload>(original.payload);
            const auto& actual = std::get<StatusPayload>(restored->payload);
            assert(actual.speed_kmh == expected.speed_kmh);
            assert(actual.rpm == expected.rpm);
            assert(actual.coolant_temp_c == expected.coolant_temp_c);
            assert(actual.engine_status == expected.engine_status);
            break;
        }
        case MessageType::CONTROL_REQ: {
            const auto& expected = std::get<ControlReqPayload>(original.payload);
            const auto& actual = std::get<ControlReqPayload>(restored->payload);
            assert(actual.command == expected.command);
            assert(actual.light == expected.light);
            assert(actual.state == expected.state);
            break;
        }
        case MessageType::CONTROL_RESP: {
            const auto& expected = std::get<ControlRespPayload>(original.payload);
            const auto& actual = std::get<ControlRespPayload>(restored->payload);
            assert(actual.result == expected.result);
            assert(actual.error_code == expected.error_code);
            assert(actual.message == expected.message);
            break;
        }
        case MessageType::HEARTBEAT:
            std::get<HeartbeatPayload>(original.payload);
            std::get<HeartbeatPayload>(restored->payload);
            break;
    }

    // 查转换前后是否一致
    assert(serialize_message(*restored) == encoded); // 将消息先json再反序列json得到的消息与原消息对比
}

int main() {
    roundtrip({DeviceId::POWER_ECU, DeviceId::CLUSTER_ECU, MessageType::STATUS, 1,
               StatusPayload{60.5, 2000, 85.0, EngineStatus::RUNNING}});
    roundtrip({DeviceId::EXTERNAL_CLIENT, DeviceId::BCM_ECU, MessageType::CONTROL_REQ, 2,
               ControlReqPayload{ControlCommand::SET_LIGHT, LightType::LOW_BEAM, LightState::ON}});
    roundtrip({DeviceId::BCM_ECU, DeviceId::EXTERNAL_CLIENT, MessageType::CONTROL_RESP, 2,
               ControlRespPayload{ControlResult::SUCCESS, ErrorCode::SUCCESS, "Light turned on"}});
    roundtrip({DeviceId::POWER_ECU, DeviceId::GATEWAY, MessageType::HEARTBEAT, 3,
               HeartbeatPayload{}});

    // 测试一：非法 JSON
    assert(!deserialize_message("{bad"));

    // 测试二：缺少必要字段
    const auto valid = serialize_message({DeviceId::POWER_ECU, DeviceId::CLUSTER_ECU,
                                          MessageType::STATUS, 1,
                                          StatusPayload{60, 2000, 85, EngineStatus::RUNNING}});
    assert(valid);
    auto j = json::parse(*valid);
    j.erase("seq");
    assert(!deserialize_message(j.dump()));

    // 序号必须从 1 开始
    j = json::parse(*valid);
    j["seq"] = 0;
    assert(!deserialize_message(j.dump()));
    Message zero_seq{DeviceId::POWER_ECU, DeviceId::GATEWAY,
                     MessageType::HEARTBEAT, 0, HeartbeatPayload{}};
    assert(!serialize_message(zero_seq));

    // 测试三：未知的枚举值
    j = json::parse(*valid);
    j["payload"]["engine_status"] = "UNKNOWN";
    assert(!deserialize_message(j.dump()));

    // 测试四：错误的消息类型
    j = json::parse(*valid);
    j["msg_type"] = 4;
    assert(!deserialize_message(j.dump()));

    // 测试五：序号超出范围
    j = json::parse(*valid);
    j["seq"] = 4294967296ULL;
    assert(!deserialize_message(j.dump()));

    // 测试六：错误的设备 ID
    j = json::parse(*valid);
    j["src_id"] = 9;
    assert(!deserialize_message(j.dump()));

    // 测试七：字段类型错误
    j = json::parse(*valid);
    j["payload"]["rpm"] = "2000";
    assert(!deserialize_message(j.dump()));

    // 最后一个测试：消息类型和 Payload 不匹配
    Message mismatch{DeviceId::POWER_ECU, DeviceId::GATEWAY, MessageType::HEARTBEAT, 1,
                     StatusPayload{}};
    assert(!serialize_message(mismatch));
}
