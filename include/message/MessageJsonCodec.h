#pragma once

#include "message/MessageTypes.h"

#include <optional>
#include <string>

namespace message {

std::optional<std::string> serialize_message(const Message& message);
std::optional<Message> deserialize_message(const std::string& json_text);

}  // namespace message
