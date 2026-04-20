#include <iostream>
#include <memory>
#include <csignal>
#include "base/log.h"
#include "base/config.h"
#include "base/threadpool.h"
#include "base/util.h"
#include "net/tcpServer.h"
#include "net/eventLoop.h"
#include "codec/codec.h"
#include "codec/protocol.h"
#include "codec/messageSerializer.h"
#include "service/authService.h"
#include "service/heartbeatService.h"
#include "service/messageService.h"
#include "service/overloadProtectService.h"

#ifdef USE_REDIS
#include "base/redisClient.h"
#endif

using namespace std;

EventLoop* g_loop = nullptr;
TcpServer* g_server = nullptr;

void SignalHandler(int sig)
{
	LOG_INFO("Received signal {}, shutting down...", sig);
	if (g_loop)
	{
		g_loop->quit();
	}
}

int main(int argc, char* argv[])
{
	Log::init("server", "logs/server.log", spdlog::level::info);

	LOG_INFO("========================================");
	LOG_INFO("HighConcurrencyTCPGateway starting...");
	LOG_INFO("========================================");

	Config& config = Config::instance();
	if (!config.load("conf/server.ini"))
	{
		LOG_ERROR("Failed to load config file");
		return -1;
	}

	int port = config.getInt("server", "port", 8888);
	int workerThreads = config.getInt("server", "worker_threads", 4);
	int heartbeatTimeout = config.getInt("heartbeat", "timeout", 30);
	int heartbeatInterval = config.getInt("heartbeat", "interval", 10);
	int maxConnections = config.getInt("overload", "max_connections", 10000);
	int maxQps = config.getInt("overload", "max_qps", 100000);

	LOG_INFO("Config loaded: port={}, workers={}", port, workerThreads);

	ThreadPool threadPool(workerThreads);
	threadPool.start();

	auto authService = make_shared<AuthService>();
	auto overloadService = make_shared<OverloadProtectService>(maxConnections, maxQps);

#ifdef USE_REDIS
	bool redisEnabled = config.getBool("redis", "enabled", false);
	shared_ptr<RedisClient> redisClient;

	if (redisEnabled)
	{
		string redisHost = config.getString("redis", "host", "127.0.0.1");
		int redisPort = config.getInt("redis", "port", 6379);
		int redisTimeout = config.getInt("redis", "timeout_ms", 2000);
		int sessionExpire = config.getInt("redis", "session_expire", 86400);

		redisClient = make_shared<RedisClient>();
		if (redisClient->connect(redisHost, redisPort, redisTimeout))
		{
			LOG_INFO("Redis connected: {}:{}", redisHost, redisPort);
			authService->setRedisClient(redisClient);
			authService->setSessionExpire(sessionExpire);
		}
		else
		{
			LOG_ERROR("Failed to connect Redis: {}", redisClient->getLastError());
			redisClient.reset();
		}
	}
#else
	LOG_INFO("Redis support not compiled in");
#endif

	EventLoop loop;
	g_loop = &loop;

	InetAddress listenAddr(static_cast<uint16_t>(port));
	TcpServer server(&loop, listenAddr, "TCPServer");
	g_server = &server;

	server.setThreadNum(workerThreads);

	Codec codec;
	auto heartbeatService = make_shared<HeartbeatService>(&loop, heartbeatTimeout, heartbeatInterval);
	auto messageService = make_shared<MessageService>();
	messageService->setAuthService(authService);

#ifdef USE_REDIS
	if (redisClient)
	{
		heartbeatService->setRedisClient(redisClient);
		heartbeatService->setThreadPool(&threadPool);
	}
#endif

	codec.setMessageCallback([&](const shared_ptr<TcpConnection>& conn,
								   uint16_t command,
								   uint32_t seqId,
								   const string& message) {
		LOG_INFO("Received message: command=0x{:04X}, seqId={}, len={}", command, seqId, message.size());

		if (command == protocol::CmdLoginReq)
		{
			protocol::LoginReq loginReq;
			serializer::Deserialize(message, loginReq);
			LOG_INFO("Login request: userId={}, token={}, deviceId={}",
					 loginReq.userId, loginReq.token, loginReq.deviceId);

			authService->login(conn, loginReq.userId, loginReq.token, loginReq.deviceId,
				[&, conn, seqId](bool success, const string& sessionId, const string& errorMsg) {
					protocol::LoginResp loginResp;
					loginResp.resultCode = success ? 0 : 1;
					loginResp.resultMsg = success ? "success" : errorMsg;
					loginResp.sessionId = sessionId;

					string respMsg = serializer::Serialize(loginResp);
					Buffer buffer;
					Codec::pack(&buffer, protocol::CmdLoginResp, seqId, respMsg);
					conn->send(&buffer);

					LOG_INFO("Login response sent: success={}, sessionId={}", success, sessionId);

#ifdef USE_REDIS
					if (success && heartbeatService)
					{
						heartbeatService->registerUserId(to_string(conn->fd()), loginReq.userId);
					}
#endif
				});
		}
		else if (command == protocol::CmdHeartbeatReq)
		{
			heartbeatService->onHeartbeat(conn);

			protocol::HeartbeatResp heartbeatResp;
			heartbeatResp.serverTime = util::GetTimestampMs();

			string respMsg = serializer::Serialize(heartbeatResp);
			Buffer buffer;
			Codec::pack(&buffer, protocol::CmdHeartbeatResp, seqId, respMsg);
			conn->send(&buffer);
		}
	});

	server.setConnectionCallback([&](const shared_ptr<TcpConnection>& conn) {
		LOG_INFO("Connection {} is {}", conn->name(),
				 conn->connected() ? "UP" : "DOWN");

		if (conn->connected())
		{
			if (!overloadService->canAcceptConnection())
			{
				LOG_WARN("Connection rejected: overload");
				conn->forceClose();
				return;
			}
			overloadService->onConnectionAccepted();
			heartbeatService->onConnection(conn, true);
		}
		else
		{
			overloadService->onConnectionClosed();
			heartbeatService->onConnection(conn, false);
			authService->logout(conn);
		}
	});

	server.setMessageCallback([&](const shared_ptr<TcpConnection>& conn,
								   Buffer* buffer,
								   Timestamp receiveTime) {
		codec.onMessage(conn, buffer, receiveTime);
	});

	heartbeatService->setKickCallback([&](const shared_ptr<TcpConnection>& conn, const string& reason) {
		conn->forceClose();
	});

	signal(SIGINT, SignalHandler);
	signal(SIGTERM, SignalHandler);

	server.start();
	heartbeatService->start();

	LOG_INFO("Server started, listening on port {}", port);

	loop.loop();

	LOG_INFO("Server shutting down...");
	heartbeatService->stop();
	threadPool.stop();

#ifdef USE_REDIS
	if (redisClient)
	{
		redisClient->disconnect();
	}
#endif

	return 0;
}
