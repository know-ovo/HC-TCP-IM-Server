#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace http
{

class MetricsServer
{
public:
    MetricsServer(std::string bindAddress, int port, std::string path);
    ~MetricsServer();

    bool start();
    void stop();

private:
    void serveLoop();

    std::string m_bindAddress;
    int m_port = 0;
    std::string m_path;
    std::atomic<bool> m_running { false };
    std::thread m_thread;
    std::intptr_t m_listenFd = -1;
};

} // namespace http
