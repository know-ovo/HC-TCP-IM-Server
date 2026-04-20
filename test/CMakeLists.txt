cmake_minimum_required(VERSION 3.16)
project(NetIntegrationTest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -pthread")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

set(BASE_SOURCES
    ${CMAKE_SOURCE_DIR}/../src/base/log.cpp
)

add_library(base STATIC ${BASE_SOURCES})

target_include_directories(base PUBLIC
    ${CMAKE_SOURCE_DIR}/../src
)

target_link_libraries(base PUBLIC spdlog::spdlog)

set(NET_SOURCES
    ${CMAKE_SOURCE_DIR}/../src/net/timestamp.cpp
    ${CMAKE_SOURCE_DIR}/../src/net/currentThread.cpp
    ${CMAKE_SOURCE_DIR}/../src/net/channel.cpp
    ${CMAKE_SOURCE_DIR}/../src/net/epollPoller.cpp
    ${CMAKE_SOURCE_DIR}/../src/net/eventLoop.cpp
)

add_library(net STATIC ${NET_SOURCES})

target_include_directories(net PUBLIC
    ${CMAKE_SOURCE_DIR}/../src
)

target_link_libraries(net PUBLIC base spdlog::spdlog)

add_executable(benchmarkClient benchmarkClient.cpp)
target_link_libraries(benchmarkClient net base spdlog::spdlog)

if(NOT MSVC)
    target_link_libraries(benchmarkClient PRIVATE pthread)
endif()

message(STATUS "=== Net Integration Test Configuration ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Net Sources: ${NET_SOURCES}")
message(STATUS "Base Sources: ${BASE_SOURCES}")
message(STATUS "Benchmark Client Sources: ${BENCHMARK_CLIENT_SOURCES}")
