#include "http/metricsServer.h"

#include <array>
#include <cstring>
#include <string>

#include "base/log.h"
#include "infra/metricsRegistry.h"

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace http
{

namespace
{

void CloseSocket(SocketHandle fd)
{
    if (fd == kInvalidSocket)
    {
        return;
    }

#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

} // namespace

MetricsServer::MetricsServer(std::string bindAddress, int port, std::string path)
    : m_bindAddress(std::move(bindAddress))
    , m_port(port)
    , m_path(std::move(path))
{
}

MetricsServer::~MetricsServer()
{
    stop();
}

bool MetricsServer::start()
{
    if (m_running.exchange(true))
    {
        return true;
    }

    m_thread = std::thread([this]() { serveLoop(); });
    return true;
}

void MetricsServer::stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }

    CloseSocket(static_cast<SocketHandle>(m_listenFd));
    m_listenFd = -1;

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void MetricsServer::serveLoop()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        LOG_ERROR("MetricsServer failed to initialize Winsock");
        m_running = false;
        return;
    }
#endif

    SocketHandle listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == kInvalidSocket)
    {
        LOG_ERROR("MetricsServer failed to create socket");
        m_running = false;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    m_listenFd = static_cast<std::intptr_t>(listenFd);

    int reuse = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse),
               static_cast<socklen_t>(sizeof(reuse)));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_port));
    if (inet_pton(AF_INET, m_bindAddress.c_str(), &addr.sin_addr) != 1)
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        LOG_ERROR("MetricsServer failed to bind {}:{}", m_bindAddress, m_port);
        CloseSocket(listenFd);
        m_listenFd = -1;
        m_running = false;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (listen(listenFd, 16) != 0)
    {
        LOG_ERROR("MetricsServer failed to listen on {}:{}", m_bindAddress, m_port);
        CloseSocket(listenFd);
        m_listenFd = -1;
        m_running = false;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    LOG_INFO("MetricsServer started on {}:{}{}", m_bindAddress, m_port, m_path);

    while (m_running.load())
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd, &readSet);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500 * 1000;

        const int ready = select(static_cast<int>(listenFd + 1), &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0)
        {
            continue;
        }

        sockaddr_in clientAddr {};
        socklen_t clientLen = static_cast<socklen_t>(sizeof(clientAddr));
        SocketHandle clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd == kInvalidSocket)
        {
            continue;
        }

        std::array<char, 4096> requestBuffer {};
        const int bytesRead = recv(clientFd, requestBuffer.data(), static_cast<int>(requestBuffer.size() - 1), 0);
        std::string responseBody = "not found\n";
        std::string statusLine = "HTTP/1.1 404 Not Found\r\n";

        if (bytesRead > 0)
        {
            const std::string request(requestBuffer.data(), static_cast<size_t>(bytesRead));
            const std::string expectedPrefix = "GET " + m_path + " ";
            if (request.rfind(expectedPrefix, 0) == 0)
            {
                responseBody = infra::MetricsRegistry::instance().renderPrometheus();
                statusLine = "HTTP/1.1 200 OK\r\n";
            }
        }

        const std::string response =
            statusLine +
            "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(responseBody.size()) + "\r\n"
            "Connection: close\r\n\r\n" +
            responseBody;

        send(clientFd, response.data(), static_cast<int>(response.size()), 0);
        CloseSocket(clientFd);
    }

    CloseSocket(listenFd);
    m_listenFd = -1;
    LOG_INFO("MetricsServer stopped");

#ifdef _WIN32
    WSACleanup();
#endif
}

} // namespace http
