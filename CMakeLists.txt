cmake_minimum_required(VERSION 3.16)

project(ecu_gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gateway apps/gateway/main.cpp)
add_executable(power_ecu apps/power_ecu/main.cpp)

add_library(protocol STATIC src/protocol/TcpFrameCodec.cpp src/protocol/TcpFrameDecoder.cpp)
target_include_directories(protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gateway PRIVATE protocol)
target_link_libraries(power_ecu PRIVATE protocol)

add_executable(tcp_frame_codec_test tests/tcp_frame_codec_test.cpp)
target_link_libraries(tcp_frame_codec_test PRIVATE protocol)

add_executable(tcp_frame_decoder_test tests/tcp_frame_decoder_test.cpp)
target_link_libraries(tcp_frame_decoder_test PRIVATE protocol)
