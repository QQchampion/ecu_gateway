cmake_minimum_required(VERSION 3.16)

project(ecu_gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json REQUIRED)

add_executable(gateway apps/gateway/main.cpp)
add_executable(power_ecu apps/power_ecu/main.cpp)
add_executable(cluster_ecu apps/cluster_ecu/main.cpp)

add_library(protocol STATIC src/protocol/TcpFrameCodec.cpp src/protocol/TcpFrameDecoder.cpp)
target_include_directories(protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gateway PRIVATE protocol)
target_link_libraries(power_ecu PRIVATE protocol)
target_link_libraries(cluster_ecu PRIVATE protocol)

add_library(message_json_codec STATIC src/message/MessageJsonCodec.cpp)
target_include_directories(message_json_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(message_json_codec PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(gateway PRIVATE message_json_codec)
target_link_libraries(power_ecu PRIVATE message_json_codec)
target_link_libraries(cluster_ecu PRIVATE message_json_codec)

add_library(connection_manager STATIC src/gateway/ConnectionManager.cpp)
target_include_directories(connection_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gateway PRIVATE connection_manager)

add_library(tcp_sender STATIC src/gateway/TcpSender.cpp)
target_include_directories(tcp_sender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tcp_sender PUBLIC connection_manager)
target_link_libraries(gateway PRIVATE tcp_sender)

add_executable(tcp_sender_test tests/tcp_sender_test.cpp)
target_link_libraries(tcp_sender_test PRIVATE tcp_sender protocol)

add_executable(tcp_frame_codec_test tests/tcp_frame_codec_test.cpp)
target_link_libraries(tcp_frame_codec_test PRIVATE protocol)

add_executable(tcp_frame_decoder_test tests/tcp_frame_decoder_test.cpp)
target_link_libraries(tcp_frame_decoder_test PRIVATE protocol)

add_executable(message_json_codec_test tests/message_json_codec_test.cpp)
target_link_libraries(message_json_codec_test PRIVATE message_json_codec nlohmann_json::nlohmann_json)

add_executable(connection_manager_test tests/connection_manager_test.cpp)
target_link_libraries(connection_manager_test PRIVATE connection_manager)
