cmake_minimum_required(VERSION 3.16)

set(BASE_SOURCES
	base/log.cpp
	base/config.cpp
	base/util.cpp
	base/threadpool.cpp
)

set(BASE_SOURCES_WITH_REDIS
	${BASE_SOURCES}
)

if(HIREDIS_FOUND)
	list(APPEND BASE_SOURCES_WITH_REDIS base/redisClient.cpp)
endif()

set(NET_SOURCES
	net/eventLoop.cpp
	net/channel.cpp
	net/epollPoller.cpp
	net/currentThread.cpp
	net/timestamp.cpp
	net/tcpServer.cpp
	net/tcpConnection.cpp
)

set(CODEC_SOURCES
	codec/buffer.cpp
	codec/codec.cpp
	codec/protocol.cpp
	codec/messageSerializer.cpp
	codec/protoCodec.cpp
)

set(STORAGE_SOURCES
	storage/mysqlClient.cpp
	storage/messageStore.cpp
	storage/messageRepository.cpp
	storage/deliveryRepository.cpp
	storage/sessionRepository.cpp
)

set(PROTO_SOURCES)
set(PROTO_HEADERS)
if(Protobuf_FOUND)
    set(PROTO_FILES
        ${CMAKE_SOURCE_DIR}/proto/common.proto
        ${CMAKE_SOURCE_DIR}/proto/auth.proto
        ${CMAKE_SOURCE_DIR}/proto/message.proto
        ${CMAKE_SOURCE_DIR}/proto/heartbeat.proto
    )
    protobuf_generate_cpp(PROTO_SOURCES PROTO_HEADERS ${PROTO_FILES})
endif()

set(SERVICE_SOURCES
	service/authService.cpp
	service/heartbeatService.cpp
	service/messageService.cpp
	service/overloadProtectService.cpp
	service/routingService.cpp
)

set(INFRA_SOURCES
	infra/rateLimiter.cpp
	infra/circuitBreaker.cpp
	infra/metricsRegistry.cpp
	infra/traceContext.cpp
)

set(HTTP_SOURCES
	http/metricsServer.cpp
)

set(ALL_SOURCES
	${BASE_SOURCES_WITH_REDIS}
	${NET_SOURCES}
	${CODEC_SOURCES}
	${STORAGE_SOURCES}
	${SERVICE_SOURCES}
	${INFRA_SOURCES}
	${HTTP_SOURCES}
	${PROTO_SOURCES}
)

add_library(server_lib STATIC ${ALL_SOURCES})
target_include_directories(server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server_lib PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json pthread)

if(Protobuf_FOUND)
	target_include_directories(server_lib PUBLIC ${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
	target_link_libraries(server_lib PUBLIC protobuf::libprotobuf)
	target_compile_definitions(server_lib PUBLIC HAVE_PROTOBUF)
endif()

if(HIREDIS_FOUND)
	target_include_directories(server_lib PUBLIC ${HIREDIS_INCLUDE_DIRS})
	target_link_libraries(server_lib PUBLIC ${HIREDIS_LIBRARIES})
	target_compile_definitions(server_lib PUBLIC USE_REDIS)
endif()

if(MYSQLCLIENT_FOUND)
	target_include_directories(server_lib PUBLIC ${MYSQLCLIENT_INCLUDE_DIRS})
	target_link_libraries(server_lib PUBLIC ${MYSQLCLIENT_LIBRARIES})
	target_compile_definitions(server_lib PUBLIC HAVE_MYSQL_CLIENT)
endif()

add_executable(server server.cpp)
target_link_libraries(server PRIVATE server_lib)

add_executable(server_full server_full.cpp)
target_link_libraries(server_full PRIVATE server_lib)

add_executable(benchmarkClient
	${BASE_SOURCES_WITH_REDIS}
	${CODEC_SOURCES}
	../tests/benchmark/benchmarkClient.cpp
)
target_include_directories(benchmarkClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(benchmarkClient PRIVATE server_lib spdlog::spdlog nlohmann_json::nlohmann_json pthread)  #添加server_lib

if(HIREDIS_FOUND)
	target_include_directories(benchmarkClient PRIVATE ${HIREDIS_INCLUDE_DIRS})
	target_link_libraries(benchmarkClient PRIVATE ${HIREDIS_LIBRARIES})
	target_compile_definitions(benchmarkClient PRIVATE USE_REDIS)
endif()

add_executable(interactiveClient
	${BASE_SOURCES}
	${CODEC_SOURCES}
	../tests/interact/interactiveClient.cpp
)
target_include_directories(interactiveClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(interactiveClient PRIVATE server_lib spdlog::spdlog nlohmann_json::nlohmann_json pthread)  #添加server_lib

if(BUILD_TESTING)
	enable_testing()

	add_executable(buffer_test ../tests/codec/buffer_test.cpp)
	target_link_libraries(buffer_test PRIVATE server_lib)

	add_executable(rateLimiter_test ../tests/infra/rateLimiter_test.cpp)
	target_link_libraries(rateLimiter_test PRIVATE server_lib)

	add_executable(circuitBreaker_test ../tests/infra/circuitBreaker_test.cpp)
	target_link_libraries(circuitBreaker_test PRIVATE server_lib)

	add_executable(overloadProtectService_test ../tests/service/overloadProtectService_test.cpp)
	target_link_libraries(overloadProtectService_test PRIVATE server_lib)

	add_executable(message_flow_test ../tests/integration/message_flow_test.cpp)
	target_link_libraries(message_flow_test PRIVATE server_lib)

	add_test(NAME buffer_test COMMAND buffer_test)
	add_test(NAME rateLimiter_test COMMAND rateLimiter_test)
	add_test(NAME circuitBreaker_test COMMAND circuitBreaker_test)
	add_test(NAME overloadProtectService_test COMMAND overloadProtectService_test)
	add_test(NAME message_flow_test COMMAND message_flow_test)
endif()

add_executable(server_benchmark_core ../tests/benchmark/core_benchmark.cpp)
target_link_libraries(server_benchmark_core PRIVATE server_lib)
