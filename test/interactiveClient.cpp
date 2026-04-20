/**
 * @file interactiveClient.cpp
 * @brief 交互式客户端（支持登录、点对点消息、广播消息）
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 支持的功能：
 * - 登录认证
 * - 心跳保活
 * - 点对点消息发送
 * - 广播消息发送
 * - 接收消息通知
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#endif

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#endif

#include "base/log.h"
#include "base/util.h"
#include "codec/codec.h"
#include "codec/protocol.h"
#include "codec/buffer.h"
#include "codec/messageSerializer.h"

using namespace std;

atomic<bool> g_running(true);
atomic<bool> g_loggedIn(false);
string g_userId;
string g_sessionId;
mutex g_consoleMutex;

#ifdef _WIN32
void InitWinsock()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed" << endl;
        exit(1);
    }
}

void CleanupWinsock()
{
    WSACleanup();
}
#endif

bool SetNonBlocking(int sockfd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sockfd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool WaitForRead(int sockfd, int timeoutMs)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);
    return ret > 0;
}

bool WaitForWrite(int sockfd, int timeoutMs)
{
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sockfd, &writefds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = select(sockfd + 1, nullptr, &writefds, nullptr, &tv);
    return ret > 0;
}

bool SendData(int sockfd, const char* data, size_t len, int timeoutMs = 5000)
{
    size_t totalSent = 0;
    while (totalSent < len)
    {
        if (!WaitForWrite(sockfd, timeoutMs))
        {
            return false;
        }

#ifdef _WIN32
        ssize_t n = send(sockfd, data + totalSent, static_cast<int>(len - totalSent), 0);
#else
        ssize_t n = write(sockfd, data + totalSent, len - totalSent);
#endif

        if (n > 0)
        {
            totalSent += n;
        }
        else
        {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINTR)
#else
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
#endif
            {
                return false;
            }
        }
    }
    return true;
}

bool RecvData(int sockfd, Buffer& buffer, int timeoutMs = 5000)
{
    if (!WaitForRead(sockfd, timeoutMs))
    {
        return false;
    }

    char readBuf[4096];
#ifdef _WIN32
    ssize_t bytesRead = recv(sockfd, readBuf, sizeof(readBuf), 0);
#else
    ssize_t bytesRead = read(sockfd, readBuf, sizeof(readBuf));
#endif

    if (bytesRead > 0)
    {
        buffer.append(readBuf, bytesRead);
        return true;
    }
    return false;
}

void ReceiveThread(int sockfd)
{
    Buffer buffer;

    while (g_running)
    {
        if (WaitForRead(sockfd, 100))
        {
            char readBuf[4096];
#ifdef _WIN32
            ssize_t bytesRead = recv(sockfd, readBuf, sizeof(readBuf), 0);
#else
            ssize_t bytesRead = read(sockfd, readBuf, sizeof(readBuf));
#endif

            if (bytesRead <= 0)
            {
                {
                    lock_guard<mutex> lock(g_consoleMutex);
                    cout << "\n[系统] 连接已断开" << endl;
                }
                g_running = false;
                break;
            }

            buffer.append(readBuf, bytesRead);

            while (buffer.readableBytes() >= protocol::kHeaderSize)
            {
                const char* data = buffer.peek();

                uint32_t netTotalLen = 0;
                memcpy(&netTotalLen, data, sizeof(netTotalLen));
                uint32_t totalLen = util::NetworkToHost32(netTotalLen);

                if (totalLen < protocol::kHeaderSize || totalLen > protocol::kMaxMessageSize)
                {
                    {
                        lock_guard<mutex> lock(g_consoleMutex);
                        cout << "\n[系统] 收到非法消息长度: " << totalLen << endl;
                    }
                    buffer.retrieveAll();
                    break;
                }

                if (buffer.readableBytes() < totalLen)
                {
                    break;
                }

                uint16_t netCommand = 0;
                memcpy(&netCommand, data + 4, sizeof(netCommand));
                uint16_t command = util::NetworkToHost16(netCommand);

                uint32_t netSeqId = 0;
                memcpy(&netSeqId, data + 6, sizeof(netSeqId));
                uint32_t seqId = util::NetworkToHost32(netSeqId);

                uint16_t netCrc = 0;
                memcpy(&netCrc, data + 10, sizeof(netCrc));
                uint16_t crc = util::NetworkToHost16(netCrc);

                uint32_t netBodyLen = 0;
                memcpy(&netBodyLen, data + 12, sizeof(netBodyLen));
                uint32_t bodyLen = util::NetworkToHost32(netBodyLen);

                if (bodyLen + protocol::kHeaderSize != totalLen)
                {
                    {
                        lock_guard<mutex> lock(g_consoleMutex);
                        cout << "\n[系统] 收到非法消息体长度: total=" << totalLen
                             << ", body=" << bodyLen << endl;
                    }
                    buffer.retrieve(totalLen);
                    continue;
                }

                string message(data + protocol::kHeaderSize, bodyLen);

                {
                    lock_guard<mutex> lock(g_consoleMutex);

                    switch (command)
                    {
                        case protocol::CmdLoginResp:
                        {
                            protocol::LoginResp resp;
                            serializer::Deserialize(message, resp);
                            if (resp.resultCode == 0)
                            {
                                g_loggedIn = true;
                                g_sessionId = resp.sessionId;
                                cout << "\n[系统] 登录成功！SessionId: " << resp.sessionId << endl;
                            }
                            else
                            {
                                cout << "\n[系统] 登录失败: " << resp.resultMsg << endl;
                            }
                            break;
                        }
                        case protocol::CmdHeartbeatResp:
                        {
                            protocol::HeartbeatResp resp;
                            serializer::Deserialize(message, resp);
                            cout << "\n[心跳] 服务器时间: " << resp.serverTime << endl;
                            break;
                        }
                        case protocol::CmdP2pMsgResp:
                        {
                            protocol::P2PMsgResp resp;
                            serializer::Deserialize(message, resp);
                            if (resp.resultCode == 0)
                            {
                                cout << "\n[消息] 点对点消息发送成功，MsgId: " << resp.msgId << endl;
                            }
                            else
                            {
                                cout << "\n[消息] 点对点消息发送失败: " << resp.resultMsg << endl;
                            }
                            break;
                        }
                        case protocol::CmdBroadcastMsgNotify:
                        {
                            protocol::BroadcastMsgNotify notify;
                            serializer::Deserialize(message, notify);
                            time_t ts = notify.timestamp / 1000;
                            char timeStr[64];
                            strftime(timeStr, sizeof(timeStr), "%H:%M:%S", localtime(&ts));
                            cout << "\n[" << timeStr << "] " << notify.fromUserId << ": " << notify.content << endl;
                            break;
                        }
                        case protocol::CmdKickUserResp:
                        {
                            protocol::KickUserResp resp;
                            serializer::Deserialize(message, resp);
                            cout << "\n[系统] 踢人结果: " << resp.resultMsg << endl;
                            break;
                        }
                        default:
                            cout << "\n[未知消息] command=0x" << hex << command << dec
                                 << ", seqId=" << seqId << ", crc=" << crc << endl;
                    }

                    cout << "> " << flush;
                }

                buffer.retrieve(totalLen);
            }
        }
    }
}

void HeartbeatThread(int sockfd)
{
    uint32_t seqId = 10000;
    
    while (g_running)
    {
        this_thread::sleep_for(chrono::seconds(10));
        
        if (!g_running || !g_loggedIn)
        {
            continue;
        }

        protocol::HeartbeatReq req;
        string msg = serializer::Serialize(req);

        Buffer buffer;
        Codec::pack(&buffer, protocol::CmdHeartbeatReq, seqId++, msg);

        if (!SendData(sockfd, buffer.peek(), buffer.readableBytes()))
        {
            lock_guard<mutex> lock(g_consoleMutex);
            cout << "\n[系统] 心跳发送失败" << endl;
        }
    }
}

void PrintHelp()
{
    cout << "\n========== 命令帮助 ==========" << endl;
    cout << "login <userId> <token> [deviceId]  - 登录" << endl;
    cout << "p2p <toUserId> <message>           - 发送点对点消息" << endl;
    cout << "broadcast <message>                - 发送广播消息" << endl;
    cout << "kick <userId> [reason]             - 踢出用户" << endl;
    cout << "help                               - 显示帮助" << endl;
    cout << "quit                               - 退出" << endl;
    cout << "================================\n" << endl;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    InitWinsock();
#endif

    Log::init("client", "logs/client.log", spdlog::level::info);

    string serverIp = "127.0.0.1";
    int port = 8888;

    for (int i = 1; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "-h" && i + 1 < argc)
        {
            serverIp = argv[++i];
        }
        else if (arg == "-p" && i + 1 < argc)
        {
            port = atoi(argv[++i]);
        }
    }

    cout << "========================================" << endl;
    cout << "     IM 交互式客户端" << endl;
    cout << "========================================" << endl;
    cout << "服务器: " << serverIp << ":" << port << endl;
    cout << "输入 'help' 查看可用命令" << endl;
    cout << "========================================\n" << endl;

#ifdef _WIN32
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
#endif

    if (sockfd < 0)
    {
        cerr << "创建 socket 失败" << endl;
        return -1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    cout << "正在连接服务器..." << endl;
    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "连接服务器失败" << endl;
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        return -1;
    }

    SetNonBlocking(sockfd);
    cout << "已连接到服务器\n" << endl;

    thread recvThread(ReceiveThread, sockfd);
    thread heartbeatThread(HeartbeatThread, sockfd);

    uint32_t seqId = 1;

    while (g_running)
    {
        {
            lock_guard<mutex> lock(g_consoleMutex);
            cout << "> " << flush;
        }

        string line;
        if (!getline(cin, line))
        {
            break;
        }

        if (line.empty())
        {
            continue;
        }

        istringstream iss(line);
        string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit")
        {
            g_running = false;
            break;
        }
        else if (cmd == "help")
        {
            PrintHelp();
        }
        else if (cmd == "login")
        {
            string userId, token, deviceId;
            iss >> userId >> token >> deviceId;
            
            if (userId.empty() || token.empty())
            {
                cout << "用法: login <userId> <token> [deviceId]" << endl;
                continue;
            }

            if (deviceId.empty())
            {
                deviceId = "default";
            }

            g_userId = userId;

            protocol::LoginReq req;
            req.userId = userId;
            req.token = token;
            req.deviceId = deviceId;

            string msg = serializer::Serialize(req);

            Buffer buffer;
            Codec::pack(&buffer, protocol::CmdLoginReq, seqId++, msg);

            if (SendData(sockfd, buffer.peek(), buffer.readableBytes()))
            {
                cout << "登录请求已发送..." << endl;
            }
            else
            {
                cout << "登录请求发送失败" << endl;
            }
        }
        else if (cmd == "p2p")
        {
            if (!g_loggedIn)
            {
                cout << "请先登录" << endl;
                continue;
            }

            string toUserId, content;
            iss >> toUserId;
            getline(iss, content);
            
            if (!content.empty() && content[0] == ' ')
            {
                content = content.substr(1);
            }

            if (toUserId.empty() || content.empty())
            {
                cout << "用法: p2p <toUserId> <message>" << endl;
                continue;
            }

            protocol::P2PMsgReq req;
            req.fromUserId = g_userId;
            req.toUserId = toUserId;
            req.content = content;

            string msg = serializer::Serialize(req);

            Buffer buffer;
            Codec::pack(&buffer, protocol::CmdP2pMsgReq, seqId++, msg);

            if (SendData(sockfd, buffer.peek(), buffer.readableBytes()))
            {
                cout << "点对点消息已发送" << endl;
            }
            else
            {
                cout << "点对点消息发送失败" << endl;
            }
        }
        else if (cmd == "broadcast" || cmd == "bc")
        {
            if (!g_loggedIn)
            {
                cout << "请先登录" << endl;
                continue;
            }

            string content;
            getline(iss, content);
            
            if (!content.empty() && content[0] == ' ')
            {
                content = content.substr(1);
            }

            if (content.empty())
            {
                cout << "用法: broadcast <message>" << endl;
                continue;
            }

            protocol::BroadcastMsgReq req;
            req.fromUserId = g_userId;
            req.content = content;

            string msg = serializer::Serialize(req);

            Buffer buffer;
            Codec::pack(&buffer, protocol::CmdBroadcastMsgReq, seqId++, msg);

            if (SendData(sockfd, buffer.peek(), buffer.readableBytes()))
            {
                cout << "广播消息已发送" << endl;
            }
            else
            {
                cout << "广播消息发送失败" << endl;
            }
        }
        else if (cmd == "kick")
        {
            if (!g_loggedIn)
            {
                cout << "请先登录" << endl;
                continue;
            }

            string targetUserId, reason;
            iss >> targetUserId;
            getline(iss, reason);
            
            if (!reason.empty() && reason[0] == ' ')
            {
                reason = reason.substr(1);
            }

            if (targetUserId.empty())
            {
                cout << "用法: kick <userId> [reason]" << endl;
                continue;
            }

            if (reason.empty())
            {
                reason = "管理员踢出";
            }

            protocol::KickUserReq req;
            req.targetUserId = targetUserId;
            req.reason = reason;

            string msg = serializer::Serialize(req);

            Buffer buffer;
            Codec::pack(&buffer, protocol::CmdKickUserReq, seqId++, msg);

            if (SendData(sockfd, buffer.peek(), buffer.readableBytes()))
            {
                cout << "踢人请求已发送" << endl;
            }
            else
            {
                cout << "踢人请求发送失败" << endl;
            }
        }
        else
        {
            cout << "未知命令: " << cmd << "，输入 'help' 查看帮助" << endl;
        }
    }

    g_running = false;

    if (recvThread.joinable())
    {
        recvThread.join();
    }
    if (heartbeatThread.joinable())
    {
        heartbeatThread.join();
    }

#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif

#ifdef _WIN32
    CleanupWinsock();
#endif

    cout << "\n客户端已退出" << endl;

    return 0;
}
