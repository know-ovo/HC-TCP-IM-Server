cmake_minimum_required(VERSION 3.16)
project(TCPServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /EHsc")
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.cache/deps" CACHE PATH "Dependency cache directory")

include(FetchContent)
include(CTest)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

find_package(Protobuf QUIET)
if(Protobuf_FOUND)
    message(STATUS "Found Protobuf: ${Protobuf_VERSION}")
else()
    message(WARNING "Protobuf not found, protobuf payload support will be disabled")
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HIREDIS hiredis)
endif()

if(NOT HIREDIS_FOUND)
    find_path(HIREDIS_INCLUDE_DIR NAMES hiredis/hiredis.h)
    find_library(HIREDIS_LIBRARY NAMES hiredis)
    if(HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARY)
        set(HIREDIS_FOUND TRUE)
        set(HIREDIS_INCLUDE_DIRS ${HIREDIS_INCLUDE_DIR})
        set(HIREDIS_LIBRARIES ${HIREDIS_LIBRARY})
    endif()
endif()

if(HIREDIS_FOUND)
    message(STATUS "Found hiredis: ${HIREDIS_LIBRARIES}")
else()
    message(WARNING "hiredis not found, Redis support will be disabled")
endif()

find_path(MYSQLCLIENT_INCLUDE_DIR NAMES mysql/mysql.h mariadb/mysql.h)
find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient mariadb libmariadb)
if(MYSQLCLIENT_INCLUDE_DIR AND MYSQLCLIENT_LIBRARY)
    set(MYSQLCLIENT_FOUND TRUE)
    set(MYSQLCLIENT_INCLUDE_DIRS ${MYSQLCLIENT_INCLUDE_DIR})
    set(MYSQLCLIENT_LIBRARIES ${MYSQLCLIENT_LIBRARY})
    message(STATUS "Found MySQL client: ${MYSQLCLIENT_LIBRARIES}")
else()
    message(WARNING "MySQL client library not found, MySQL persistence will be disabled")
endif()

add_subdirectory(src)
