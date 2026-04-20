/**
 * @file server_full.cpp
 * @brief 完整版服务器入口（包含点对点消息和广播消息功能）
 * @author IM Server Team
 * @version 2.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件是服务器的完整版入口，相比 server.cpp 增加了：
 * - 点对点消息处理
 * - 广播消息处理
 * - 用户连接注册管理
 */

#include <iostream>
#include <memory>
#include <algorithm>
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
#include "codec/protoCodec.h"
#include "infra/circuitBreaker.h"
#include "infra/metricsRegistry.h"
#include "infra/traceContext.h"
#include "http/metricsServer.h"
#include "service/authService.h"
#include "service/heartbeatService.h"
#include "service/messageService.h"
#include "service/overloadProtectService.h"
#include "service/routingService.h"
#include "storage/mysqlClient.h"
#include "storage/messageStore.h"
#include "storage/sessionRepository.h"

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

int main(int /*argc*/, char* /*argv*/[])
{
    Log::init("server_full", "logs/server_full.log", spdlog::level::info);

    LOG_INFO("========================================");
    LOG_INFO("HighConcurrencyTCPGateway (Full) starting...");
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
    string epollMode = config.getString("net", "epoll_mode", "lt");
    bool tcpNoDelay = config.getBool("net", "tcp_nodelay", true);
    int highWaterMarkBytes = config.getInt("net", "high_water_mark_bytes", 1024 * 1024);
    int lowWaterMarkBytes = config.getInt("net", "low_water_mark_bytes", 256 * 1024);
    int maxOutputBufferBytes = config.getInt("net", "max_output_buffer_bytes", 4 * 1024 * 1024);
    string payloadFormatStr = config.getString("protocol", "payload_format", "json");
    int protocolVersion = config.getInt("protocol", "version", protocol::kProtocolVersionV1);
    bool clusterEnabled = config.getBool("cluster", "enabled", false);
    string nodeId = config.getString("cluster", "node_id", "node-1");
    int nodeAliveTtlSeconds = config.getInt("cluster", "node_alive_ttl_seconds", 15);
    int routeTtlSeconds = config.getInt("cluster", "route_ttl_seconds", 30);
    bool mysqlEnabled = config.getBool("mysql", "enabled", false);
    string mysqlHost = config.getString("mysql", "host", "127.0.0.1");
    int mysqlPort = config.getInt("mysql", "port", 3306);
    string mysqlUser = config.getString("mysql", "user", "root");
    string mysqlPassword = config.getString("mysql", "password", "");
    string mysqlDatabase = config.getString("mysql", "database", "im_server");
    int mysqlConnectTimeoutMs = config.getInt("mysql", "connect_timeout_ms", 2000);
    int reliableRetryIntervalMs = config.getInt("reliable", "retry_interval_ms", 5000);
    int reliableAckTimeoutMs = config.getInt("reliable", "ack_timeout_ms", 15000);
    int reliableRetryBackoffMs = config.getInt("reliable", "retry_backoff_ms", 3000);
    int reliableMaxRetryCount = config.getInt("reliable", "max_retry_count", 5);
    int reliableRetryBatchSize = config.getInt("reliable", "retry_batch_size", 200);
    bool rateLimitEnabled = config.getBool("rate_limit", "enable", false);
    int maxConnectPerIpPerSec = config.getInt("rate_limit", "max_connect_per_ip_per_sec", 20);
    int loginQps = config.getInt("rate_limit", "login_qps", 2000);
    int p2pQps = config.getInt("rate_limit", "p2p_qps", 10000);
    int broadcastQps = config.getInt("rate_limit", "broadcast_qps", 200);
    int userSendPerSec = config.getInt("rate_limit", "user_send_per_sec", 50);
    bool circuitBreakerEnabled = config.getBool("circuit_breaker", "enable", true);
    int threadPoolFailureThreshold = config.getInt("circuit_breaker", "thread_pool_failure_threshold", 10);
    int halfOpenAfterMs = config.getInt("circuit_breaker", "half_open_after_ms", 5000);
    int authTimeoutSeconds = config.getInt("connection", "auth_timeout_seconds", 10);
    int idleTimeoutSeconds = config.getInt("connection", "idle_timeout_seconds", 300);
    int slowConsumerKickSeconds = config.getInt("connection", "slow_consumer_kick_seconds", 30);
    bool metricsEnabled = config.getBool("metrics", "enable", false);
    string metricsBind = config.getString("metrics", "bind", "0.0.0.0");
    int metricsPort = config.getInt("metrics", "port", 9100);
    string metricsPath = config.getString("metrics", "path", "/metrics");

    auto payloadFormat = ProtoCodec::parsePayloadFormat(payloadFormatStr);
    if (payloadFormat == protocol::PayloadFormat::Protobuf && !ProtoCodec::protobufAvailable())
    {
        LOG_WARN("protobuf payload requested but protobuf support is not compiled in, fallback to json");
        payloadFormat = protocol::PayloadFormat::Json;
    }

    LOG_INFO("Config loaded: port={}, workers={}, epoll_mode={}, payload_format={}, protocol_version={}, tcp_nodelay={}",
             port,
             workerThreads,
             epollMode,
             ProtoCodec::toString(payloadFormat),
             protocolVersion,
             tcpNoDelay);

    auto& metricsRegistry = infra::MetricsRegistry::instance();
    metricsRegistry.setEnabled(metricsEnabled);
    metricsRegistry.setGauge("im_online_connections", 0, "Current accepted connections");
    metricsRegistry.setGauge("im_threadpool_queue_size", 0, "Current thread pool queue size");
    metricsRegistry.setGauge("im_output_buffer_bytes", 0, "Latest observed connection output buffer bytes");

    ThreadPool threadPool(workerThreads);
    const int businessMaxQueueSize = config.getInt("thread_pool", "max_queue_size", 10000);
    auto updateThreadPoolMetrics = [&]() {
        metricsRegistry.setGauge("im_threadpool_queue_size",
                                 static_cast<double>(threadPool.queueSize()),
                                 "Current thread pool queue size");
    };
    threadPool.setMaxQueueSize(static_cast<size_t>(businessMaxQueueSize));
    threadPool.setRejectCallback([&]() {
        updateThreadPoolMetrics();
        LOG_WARN("ThreadPool rejected task: queue_size={}, max_queue_size={}",
                 threadPool.queueSize(),
                 businessMaxQueueSize);
    });
    threadPool.start();

    std::unique_ptr<http::MetricsServer> metricsServer;
    if (metricsEnabled)
    {
        metricsServer = std::make_unique<http::MetricsServer>(metricsBind, metricsPort, metricsPath);
        metricsServer->start();
    }

    auto authService = make_shared<AuthService>();
    auto overloadService = make_shared<OverloadProtectService>(maxConnections, maxQps);
    auto messageService = make_shared<MessageService>();
    auto routingService = make_shared<RoutingService>();
    infra::CircuitBreaker businessBreaker(circuitBreakerEnabled,
                                          static_cast<uint32_t>(threadPoolFailureThreshold),
                                          halfOpenAfterMs);
    messageService->setAuthService(authService);
    authService->setNodeId(nodeId);
    overloadService->setRateLimitEnabled(rateLimitEnabled);
    overloadService->configureIpConnectRate(static_cast<double>(maxConnectPerIpPerSec));
    overloadService->configureCommandRates(static_cast<double>(loginQps),
                                          static_cast<double>(p2pQps),
                                          static_cast<double>(broadcastQps));
    overloadService->configureUserSendRate(static_cast<double>(userSendPerSec));

    shared_ptr<storage::IMessageStore> messageStore = make_shared<storage::InMemoryMessageStore>();
    shared_ptr<storage::SessionRepository> sessionRepository;
    if (mysqlEnabled)
    {
        storage::MySqlStoreOptions mysqlOptions;
        mysqlOptions.enabled = true;
        mysqlOptions.host = mysqlHost;
        mysqlOptions.port = mysqlPort;
        mysqlOptions.user = mysqlUser;
        mysqlOptions.password = mysqlPassword;
        mysqlOptions.database = mysqlDatabase;
        mysqlOptions.connectTimeoutMs = mysqlConnectTimeoutMs;

        auto mysqlStore = make_shared<storage::MySqlMessageStore>(mysqlOptions);
        string mysqlError;
        if (mysqlStore->init(mysqlError))
        {
            messageStore = mysqlStore;
            LOG_INFO("Reliable message store initialized with mysql: {}:{} / {}",
                     mysqlHost,
                     mysqlPort,
                     mysqlDatabase);
        }
        else
        {
            LOG_WARN("MySQL store init failed, fallback to memory store: {}", mysqlError);
        }

        auto sessionClient = make_shared<MySqlClient>();
        if (sessionClient->connect(mysqlHost,
                                   mysqlPort,
                                   mysqlUser,
                                   mysqlPassword,
                                   mysqlDatabase,
                                   mysqlConnectTimeoutMs))
        {
            sessionRepository = make_shared<storage::SessionRepository>(sessionClient);
            authService->setSessionRepository(sessionRepository);
            LOG_INFO("Session repository initialized with mysql: {}:{} / {}",
                     mysqlHost,
                     mysqlPort,
                     mysqlDatabase);
        }
        else
        {
            LOG_WARN("Session repository init failed: {}", sessionClient->lastError());
        }
    }

    if (messageStore->name() == std::string("memory"))
    {
        string storeError;
        if (!messageStore->init(storeError))
        {
            LOG_ERROR("Failed to initialize message store: {}", storeError);
            return -1;
        }
    }
    messageService->setMessageStore(messageStore);
    messageService->setSessionRepository(sessionRepository);
    messageService->setRetryConfig(reliableRetryIntervalMs,
                                   static_cast<size_t>(reliableRetryBatchSize));
    messageService->setRetryPolicy(reliableAckTimeoutMs,
                                   reliableRetryBackoffMs,
                                   static_cast<uint32_t>(reliableMaxRetryCount));
    LOG_INFO("Reliable message store mode: {}", messageStore->name());

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
            routingService->setRedisClient(redisClient);
            routingService->setNodeId(nodeId);
            routingService->setRouteTtlSeconds(routeTtlSeconds);
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
    loop.setUseEdgeTrigger(util::ToLower(epollMode) == "et");

    InetAddress listenAddr(static_cast<uint16_t>(port));
    TcpServer server(&loop, listenAddr, "TCPServer");
    g_server = &server;

    server.setThreadNum(workerThreads);

    Codec codec;
    auto heartbeatService = make_shared<HeartbeatService>(&loop, heartbeatTimeout, heartbeatInterval);

#ifdef USE_REDIS
    if (redisClient)
    {
        heartbeatService->setRedisClient(redisClient);
        heartbeatService->setThreadPool(&threadPool);
    }
#endif
    heartbeatService->setNodeId(nodeId);
    heartbeatService->setNodeAliveTtlSeconds(nodeAliveTtlSeconds);
    heartbeatService->setConnectionGovernance(authTimeoutSeconds, idleTimeoutSeconds, slowConsumerKickSeconds);

    auto sendPacket = [&](const shared_ptr<TcpConnection>& conn,
                          uint16_t command,
                          uint32_t requestId,
                          const string& payload,
                          uint16_t flags = 0,
                          uint64_t clientSeq = 0,
                          uint64_t serverSeq = 0,
                          uint32_t errorCode = protocol::ErrOk) {
        Buffer buffer;
        protocol::PacketHeader header;
        header.command = command;
        header.flags = flags == 0 ? protocol::GuessFlagsFromCommand(command) : flags;
        header.requestId = requestId;
        header.clientSeq = clientSeq;
        header.serverSeq = serverSeq;
        header.errorCode = errorCode;
        Codec::pack(&buffer, header, payload);
        conn->send(&buffer);
    };

    auto submitBusinessTask = [&](const std::string& taskName,
                                  const infra::TraceContext& traceContext,
                                  auto&& task) {
        if (!businessBreaker.allowRequest() && taskName != "login" && taskName != "ack")
        {
            LOG_WARN("Circuit breaker open, reject task [{}]", taskName);
            return false;
        }
        try
        {
            threadPool.submit([traceContext, task = std::forward<decltype(task)>(task)]() mutable {
                infra::ScopedTraceContext scope(traceContext);
                task();
            });
            updateThreadPoolMetrics();
            businessBreaker.recordSuccess();
            return true;
        }
        catch (const std::exception& e)
        {
            updateThreadPoolMetrics();
            LOG_WARN("Failed to submit business task [{}]: {}", taskName, e.what());
            businessBreaker.recordFailure();
            return false;
        }
    };

    codec.setPacketCallback([&](const shared_ptr<TcpConnection>& conn,
                                const Codec::DecodedPacket& packet) {
        const uint16_t command = packet.header.command;
        const uint32_t requestId = packet.header.requestId;
        const string& message = packet.payload;
        infra::TraceContext packetTrace;
        packetTrace.traceId = infra::GenerateTraceId();
        packetTrace.requestId = requestId;
        packetTrace.nodeId = nodeId;
        if (const auto currentUser = authService->getUserContext(conn))
        {
            packetTrace.userId = currentUser->m_userId;
            packetTrace.sessionId = currentUser->m_sessionId;
        }
        infra::ScopedTraceContext packetScope(packetTrace);
        LOG_INFO("Received packet: command=0x{:04X}, requestId={}, flags=0x{:04X}, version={}, len={}",
                 command,
                 requestId,
                 packet.header.flags,
                 packet.header.version,
                 message.size());

        if ((command == protocol::CmdLoginReq ||
             command == protocol::CmdP2pMsgReq ||
             command == protocol::CmdBroadcastMsgReq) &&
            !overloadService->canProcessCommand(command))
        {
            LOG_WARN("Command rejected by rate limiter: command=0x{:04X}", command);
            if (command == protocol::CmdLoginReq)
            {
                protocol::LoginResp resp;
                resp.resultCode = 1;
                resp.resultMsg = "rate limited";
                sendPacket(conn, protocol::CmdLoginResp, requestId, serializer::Serialize(resp),
                           protocol::kFlagResponse, 0, 0, protocol::ErrServerBusy);
            }
            else if (command == protocol::CmdP2pMsgReq)
            {
                protocol::P2PMsgResp resp{};
                resp.resultCode = 1;
                resp.resultMsg = "rate limited";
                sendPacket(conn, protocol::CmdP2pMsgResp, requestId, serializer::Serialize(resp),
                           protocol::kFlagResponse, 0, 0, protocol::ErrServerBusy);
            }
            else if (command == protocol::CmdBroadcastMsgReq)
            {
                LOG_WARN("Broadcast dropped because rate limiter rejected it");
            }
            return;
        }

        if (command == protocol::CmdLoginReq)
        {
            protocol::LoginReq loginReq;
            serializer::Deserialize(message, loginReq);
            metricsRegistry.incCounter("im_login_qps", 1, "Inbound login request count");
            infra::TraceContext loginTrace = infra::CurrentTraceContext();
            loginTrace.userId = loginReq.userId;
            infra::ScopedTraceContext loginScope(loginTrace);
            LOG_INFO("Login request: userId={}, token={}, deviceId={}",
                     loginReq.userId, loginReq.token, loginReq.deviceId);
            if (!submitBusinessTask("login", loginTrace, [&, conn, requestId, loginReq, loginTrace]() {
                    authService->login(conn, loginReq.userId, loginReq.token, loginReq.deviceId,
                        [&, conn, requestId, loginReq, loginTrace](bool success, const string& sessionId, const string& errorMsg) {
                            infra::TraceContext responseTrace = loginTrace;
                            responseTrace.sessionId = sessionId;
                            infra::ScopedTraceContext responseScope(responseTrace);
                            protocol::LoginResp loginResp;
                            loginResp.resultCode = success ? 0 : 1;
                            loginResp.resultMsg = success ? "success" : errorMsg;
                            loginResp.sessionId = sessionId;

                            sendPacket(conn,
                                       protocol::CmdLoginResp,
                                       requestId,
                                       serializer::Serialize(loginResp),
                                       protocol::kFlagResponse,
                                       0,
                                       0,
                                       success ? protocol::ErrOk : protocol::ErrAuthFailed);

                            LOG_INFO("Login response sent: success={}, sessionId={}", success, sessionId);

                            if (success)
                            {
                                authService->bindSessionToNode(sessionId, nodeId);
                                messageService->registerConnection(loginReq.userId, conn);
                                messageService->deliverPendingMessages(loginReq.userId);
                                heartbeatService->onAuthenticated(conn, loginReq.userId);
                                if (clusterEnabled)
                                {
                                    routingService->bindUserRoute(loginReq.userId, nodeId);
                                }
                            }
                        });
                }))
            {
                protocol::LoginResp loginResp;
                loginResp.resultCode = 1;
                loginResp.resultMsg = "server busy";
                sendPacket(conn,
                           protocol::CmdLoginResp,
                           requestId,
                           serializer::Serialize(loginResp),
                           protocol::kFlagResponse,
                           0,
                           0,
                           protocol::ErrServerBusy);
            }
        }
        else if (command == protocol::CmdHeartbeatReq)
        {
            heartbeatService->onHeartbeat(conn);

            protocol::HeartbeatResp heartbeatResp;
            heartbeatResp.serverTime = util::GetTimestampMs();

            sendPacket(conn,
                       protocol::CmdHeartbeatResp,
                       requestId,
                       serializer::Serialize(heartbeatResp),
                       protocol::kFlagResponse);
        }
        else if (command == protocol::CmdP2pMsgReq)
        {
            protocol::P2PMsgReq p2pReq{};
            const bool parsed = serializer::Deserialize(message, p2pReq);
            auto userContext = authService->getUserContext(conn);
            metricsRegistry.incCounter("im_p2p_qps", 1, "Inbound p2p request count");

            protocol::P2PMsgResp p2pResp{};
            p2pResp.resultCode = 1;
            p2pResp.resultMsg = "invalid request";
            p2pResp.msgId = 0;
            p2pResp.serverSeq = 0;

            if (!parsed)
            {
                p2pResp.resultMsg = "invalid p2p payload";
            }
            else if (!userContext)
            {
                p2pResp.resultMsg = "unauthenticated";
            }
            else
            {
                const string fromUserId = userContext->m_userId;
                infra::TraceContext p2pTrace = infra::CurrentTraceContext();
                p2pTrace.userId = fromUserId;
                infra::ScopedTraceContext p2pScope(p2pTrace);
                if (!overloadService->canSendMessage(fromUserId))
                {
                    p2pResp.resultCode = 1;
                    p2pResp.resultMsg = "user send rate limited";
                    sendPacket(conn,
                               protocol::CmdP2pMsgResp,
                               requestId,
                               serializer::Serialize(p2pResp),
                               protocol::kFlagResponse,
                               0,
                               0,
                               protocol::ErrServerBusy);
                    return;
                }
                if (!submitBusinessTask("p2p", p2pTrace, [&, conn, requestId, p2pReq, fromUserId, p2pTrace]() {
                        protocol::P2PMsgResp asyncResp{};
                        asyncResp.resultCode = 1;
                        asyncResp.resultMsg = "server error";
                        asyncResp.msgId = 0;
                        asyncResp.serverSeq = 0;

                        if (clusterEnabled)
                        {
                            const auto routeNode = routingService->lookupUserRoute(p2pReq.toUserId);
                            if (routeNode && *routeNode != nodeId)
                            {
                                LOG_WARN("Remote route detected but forwarding not enabled: toUserId={}, targetNode={}, localNode={}",
                                         p2pReq.toUserId,
                                         *routeNode,
                                         nodeId);
                                asyncResp.resultCode = 1;
                                asyncResp.resultMsg = "remote forwarding not enabled";
                                sendPacket(conn,
                                           protocol::CmdP2pMsgResp,
                                           requestId,
                                           serializer::Serialize(asyncResp),
                                           protocol::kFlagResponse,
                                           0,
                                           0,
                                           protocol::ErrServerBusy);
                                return;
                            }
                        }

                        uint64_t msgId = 0;
                        uint64_t serverSeq = 0;
                        string resultMsg;
                        const bool success = messageService->sendReliableP2PMessage(fromUserId,
                                                                                    p2pReq.toUserId,
                                                                                    p2pReq.clientMsgId,
                                                                                    p2pReq.content,
                                                                                    msgId,
                                                                                    serverSeq,
                                                                                    resultMsg);

                        infra::TraceContext responseTrace = p2pTrace;
                        responseTrace.messageId = msgId;
                        responseTrace.serverSeq = serverSeq;
                        infra::ScopedTraceContext responseScope(responseTrace);

                        LOG_INFO("P2P reliable request: from={}, to={}, clientMsgId={}, msgId={}, serverSeq={}, success={}",
                                 fromUserId,
                                 p2pReq.toUserId,
                                 p2pReq.clientMsgId,
                                 msgId,
                                 serverSeq,
                                 success);

                        asyncResp.resultCode = success ? 0 : 1;
                        asyncResp.resultMsg = resultMsg;
                        asyncResp.msgId = msgId;
                        asyncResp.serverSeq = serverSeq;

                        sendPacket(conn,
                                   protocol::CmdP2pMsgResp,
                                   requestId,
                                   serializer::Serialize(asyncResp),
                                   protocol::kFlagResponse,
                                   asyncResp.msgId,
                                   asyncResp.serverSeq,
                                   asyncResp.resultCode == 0 ? protocol::ErrOk : protocol::ErrInvalidPacket);
                    }))
                {
                    p2pResp.resultMsg = "server busy";
                    p2pResp.resultCode = 1;
                    sendPacket(conn,
                               protocol::CmdP2pMsgResp,
                               requestId,
                               serializer::Serialize(p2pResp),
                               protocol::kFlagResponse,
                               0,
                               0,
                               protocol::ErrServerBusy);
                }
                return;
            }

            sendPacket(conn,
                       protocol::CmdP2pMsgResp,
                       requestId,
                       serializer::Serialize(p2pResp),
                       protocol::kFlagResponse,
                       p2pResp.msgId,
                       p2pResp.serverSeq,
                       p2pResp.resultCode == 0 ? protocol::ErrOk : protocol::ErrInvalidPacket);
        }
        else if (command == protocol::CmdMessageAckReq)
        {
            protocol::MessageAckReq ackReq{};
            const bool parsed = serializer::Deserialize(message, ackReq);
            auto userContext = authService->getUserContext(conn);

            protocol::MessageAckResp ackResp{};
            ackResp.resultCode = 1;
            ackResp.resultMsg = "invalid request";
            ackResp.serverSeq = 0;

            if (!parsed)
            {
                ackResp.resultMsg = "invalid ack payload";
            }
            else if (!userContext)
            {
                ackResp.resultMsg = "unauthenticated";
            }
            else
            {
                const string userId = userContext->m_userId;
                infra::TraceContext ackTrace = infra::CurrentTraceContext();
                ackTrace.userId = userId;
                ackTrace.messageId = ackReq.msgId;
                ackTrace.serverSeq = ackReq.serverSeq;
                if (!submitBusinessTask("ack", ackTrace, [&, conn, requestId, ackReq, userId, ackTrace]() {
                        string errorMsg;
                        uint64_t lastAckedSeq = 0;
                        const bool success = messageService->processAck(userId,
                                                                        ackReq.msgId,
                                                                        ackReq.serverSeq,
                                                                        ackReq.ackCode,
                                                                        lastAckedSeq,
                                                                        errorMsg);
                        infra::TraceContext responseTrace = ackTrace;
                        responseTrace.serverSeq = lastAckedSeq;
                        infra::ScopedTraceContext responseScope(responseTrace);
                        protocol::MessageAckResp asyncResp{};
                        asyncResp.resultCode = success ? 0 : 1;
                        asyncResp.resultMsg = success ? "success" : errorMsg;
                        asyncResp.serverSeq = lastAckedSeq;

                        sendPacket(conn,
                                   protocol::CmdMessageAckResp,
                                   requestId,
                                   serializer::Serialize(asyncResp),
                                   protocol::kFlagResponse | protocol::kFlagAck,
                                   ackReq.msgId,
                                   asyncResp.serverSeq,
                                   asyncResp.resultCode == 0 ? protocol::ErrOk : protocol::ErrInvalidPacket);
                    }))
                {
                    ackResp.resultMsg = "server busy";
                    ackResp.resultCode = 1;
                    sendPacket(conn,
                               protocol::CmdMessageAckResp,
                               requestId,
                               serializer::Serialize(ackResp),
                               protocol::kFlagResponse | protocol::kFlagAck,
                               ackReq.msgId,
                               0,
                               protocol::ErrServerBusy);
                }
                return;
            }

            sendPacket(conn,
                       protocol::CmdMessageAckResp,
                       requestId,
                       serializer::Serialize(ackResp),
                       protocol::kFlagResponse | protocol::kFlagAck,
                       ackReq.msgId,
                       ackResp.serverSeq,
                       ackResp.resultCode == 0 ? protocol::ErrOk : protocol::ErrInvalidPacket);
        }
        else if (command == protocol::CmdPullOfflineReq)
        {
            protocol::PullOfflineReq pullReq{};
            const bool parsed = serializer::Deserialize(message, pullReq);
            auto userContext = authService->getUserContext(conn);

            protocol::PullOfflineResp pullResp{};
            pullResp.resultCode = 1;
            pullResp.resultMsg = "invalid request";
            pullResp.nextBeginSeq = 0;

            if (!parsed)
            {
                pullResp.resultMsg = "invalid pull payload";
            }
            else if (!userContext)
            {
                pullResp.resultMsg = "unauthenticated";
            }
            else
            {
                const string userId = userContext->m_userId;
                infra::TraceContext pullTrace = infra::CurrentTraceContext();
                pullTrace.userId = userId;
                pullTrace.serverSeq = pullReq.lastAckedSeq;
                if (!submitBusinessTask("pull_offline", pullTrace, [&, conn, requestId, pullReq, userId, pullTrace]() {
                        protocol::PullOfflineResp asyncResp{};
                        asyncResp.resultCode = 0;
                        asyncResp.resultMsg = "success";
                        asyncResp.nextBeginSeq = 0;
                        size_t limit = pullReq.limit == 0 ? 100 : static_cast<size_t>(pullReq.limit);
                        if (businessBreaker.isOpen())
                        {
                            limit = std::min<size_t>(limit, 20);
                        }
                        asyncResp.messages = messageService->pullOfflineMessages(userId,
                                                                                pullReq.lastAckedSeq,
                                                                                limit,
                                                                                asyncResp.nextBeginSeq);
                        sendPacket(conn,
                                   protocol::CmdPullOfflineResp,
                                   requestId,
                                   serializer::Serialize(asyncResp),
                                   protocol::kFlagResponse,
                                   0,
                                   asyncResp.nextBeginSeq,
                                   protocol::ErrOk);
                    }))
                {
                    pullResp.resultMsg = "server busy";
                    pullResp.resultCode = 1;
                    sendPacket(conn,
                               protocol::CmdPullOfflineResp,
                               requestId,
                               serializer::Serialize(pullResp),
                               protocol::kFlagResponse,
                               0,
                               0,
                               protocol::ErrServerBusy);
                }
                return;
            }

            sendPacket(conn,
                       protocol::CmdPullOfflineResp,
                       requestId,
                       serializer::Serialize(pullResp),
                       protocol::kFlagResponse,
                       0,
                       pullResp.nextBeginSeq,
                       pullResp.resultCode == 0 ? protocol::ErrOk : protocol::ErrInvalidPacket);
        }
        else if (command == protocol::CmdBroadcastMsgReq)
        {
            protocol::BroadcastMsgReq broadcastReq;
            const bool parsed = serializer::Deserialize(message, broadcastReq);
            auto userContext = authService->getUserContext(conn);
            if (!parsed || !userContext)
            {
                LOG_WARN("Broadcast request rejected: parsed={}, authenticated={}",
                         parsed,
                         userContext != nullptr);
            }
            else
            {
                LOG_INFO("Broadcast message request: from={}, content={}",
                         userContext->m_userId,
                         broadcastReq.content);
                const string fromUserId = userContext->m_userId;
                infra::TraceContext broadcastTrace = infra::CurrentTraceContext();
                broadcastTrace.userId = fromUserId;
                if (!submitBusinessTask("broadcast", broadcastTrace, [&, fromUserId, broadcastReq]() {
                        messageService->broadcastMessage(fromUserId, broadcastReq.content);
                        LOG_INFO("Broadcast message sent to all online users");
                    }))
                {
                    LOG_WARN("Broadcast request dropped: thread pool busy, from={}", fromUserId);
                }
            }
        }
        else if (command == protocol::CmdKickUserReq)
        {
            protocol::KickUserReq kickReq;
            serializer::Deserialize(message, kickReq);
            LOG_INFO("Kick user request: target={}, reason={}",
                     kickReq.targetUserId, kickReq.reason);

            protocol::KickUserResp kickResp;
            kickResp.resultCode = 0;
            kickResp.resultMsg = "success";

            sendPacket(conn,
                       protocol::CmdKickUserResp,
                       requestId,
                       serializer::Serialize(kickResp),
                       protocol::kFlagResponse);

            LOG_INFO("Kick user response sent: target={}", kickReq.targetUserId);
        }
        else
        {
            LOG_WARN("Unknown command: 0x{:04X}", command);
        }
    });

    server.setConnectionCallback([&](const shared_ptr<TcpConnection>& conn) {
        LOG_INFO("Connection {} is {}", conn->name(),
                 conn->connected() ? "UP" : "DOWN");

        if (conn->connected())
        {
            conn->setHighWaterMark(static_cast<size_t>(highWaterMarkBytes));
            conn->setLowWaterMark(static_cast<size_t>(lowWaterMarkBytes));
            conn->setMaxOutputBufferBytes(static_cast<size_t>(maxOutputBufferBytes));
            conn->setHighWaterMarkCallback([&](const shared_ptr<TcpConnection>& c, size_t bytes) {
                metricsRegistry.setGauge("im_output_buffer_bytes",
                                         static_cast<double>(bytes),
                                         "Latest observed connection output buffer bytes");
                LOG_WARN("Connection {} entered backpressure state, pending_bytes={}", c->name(), bytes);
                heartbeatService->onWriteBlocked(c);
            });
            conn->setWriteCompleteCallback([&](const shared_ptr<TcpConnection>& c) {
                metricsRegistry.setGauge("im_output_buffer_bytes",
                                         static_cast<double>(c->outputBufferBytes()),
                                         "Latest observed connection output buffer bytes");
                heartbeatService->onBackPressureRecovered(c);
            });
            if (!overloadService->canAcceptConnection(conn->peerAddress().toIp()))
            {
                LOG_WARN("Connection rejected: overload");
                conn->forceClose();
                return;
            }
            overloadService->onConnectionAccepted();
            metricsRegistry.setGauge("im_online_connections",
                                     static_cast<double>(overloadService->getCurrentConnections()),
                                     "Current accepted connections");
            heartbeatService->onConnection(conn, true);
        }
        else
        {
            overloadService->onConnectionClosed();
            metricsRegistry.setGauge("im_online_connections",
                                     static_cast<double>(overloadService->getCurrentConnections()),
                                     "Current accepted connections");
            heartbeatService->onConnection(conn, false);
            
            auto userContext = authService->getUserContext(conn);
            if (userContext)
            {
                messageService->unregisterConnection(userContext->m_userId);
                if (clusterEnabled)
                {
                    routingService->unbindUserRoute(userContext->m_userId);
                }
            }

            if (!submitBusinessTask("logout", infra::CurrentTraceContext(), [&, conn]() {
                    authService->logout(conn);
                }))
            {
                LOG_WARN("Logout task dropped because thread pool is busy");
            }
        }
    });

    server.setMessageCallback([&](const shared_ptr<TcpConnection>& conn,
                                  Buffer* buffer,
                                  Timestamp receiveTime) {
        codec.onMessage(conn, buffer, receiveTime);
    });

    heartbeatService->setKickCallback([&](const shared_ptr<TcpConnection>& conn, const string& reason) {
        LOG_WARN("Kicking connection: {}, reason: {}", conn->name(), reason);
        conn->forceClose();
    });

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    server.start();
    heartbeatService->start();
    messageService->start();

    LOG_INFO("Server (Full) started, listening on port {}", port);

    loop.loop();

    LOG_INFO("Server shutting down...");
    messageService->stop();
    heartbeatService->stop();
    threadPool.stop();
    if (metricsServer)
    {
        metricsServer->stop();
    }

#ifdef USE_REDIS
    if (redisClient)
    {
        redisClient->disconnect();
    }
#endif

    return 0;
}
