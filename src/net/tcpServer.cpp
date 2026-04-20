// TcpServer.cpp
#include "net/tcpServer.h"
#include "base/log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace
{

int createNonblockingSocket()
{
	int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
	if (sockfd < 0)
	{
		LOG_ERROR("TcpServer::createNonblockingSocket - socket error: {}", errno);
	}
	return sockfd;
}

}

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name)
	: m_loop(loop)
	, m_name(name)
	, m_listenfd(createNonblockingSocket())
	, m_acceptChannel(new Channel(loop, m_listenfd))
	, m_started(0)
	, m_nextConnId(1)
{
	int optval = 1;
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	struct sockaddr_in addr = listenAddr.getSockAddrInet();
	if (bind(m_listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		LOG_ERROR("TcpServer::TcpServer - bind error: {}", errno);
	}

	m_acceptChannel->setReadCallback([this](Timestamp) { newConnection(); });
}

TcpServer::~TcpServer()
{
	m_acceptChannel->disableAll();
	m_acceptChannel->remove();
	close(m_listenfd);

	for (auto& pair : m_connections)
	{
		pair.second->forceClose();
	}
}

void TcpServer::setThreadNum(int numThreads)
{
	(void)numThreads;
}

void TcpServer::start()
{
	if (m_started.exchange(1) == 0)
	{
		if (listen(m_listenfd, SOMAXCONN) < 0)
		{
			LOG_ERROR("TcpServer::start - listen error: {}", errno);
			return;
		}
		m_acceptChannel->enableReading();
		LOG_INFO("TcpServer::start - listening on fd {}", m_listenfd);
	}
}

void TcpServer::newConnection()
{
	struct sockaddr_in peerAddr;
	socklen_t peerLen = sizeof(peerAddr);
	int connfd = accept4(m_listenfd, (struct sockaddr*)&peerAddr, &peerLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (connfd < 0)
	{
		LOG_ERROR("TcpServer::newConnection - accept error: {}", errno);
		return;
	}

	char buf[64];
	snprintf(buf, sizeof(buf), ":%s#%d", m_name.c_str(), m_nextConnId++);
	std::string connName = buf;

	struct sockaddr_in localAddr;
	socklen_t localLen = sizeof(localAddr);
	getsockname(connfd, (struct sockaddr*)&localAddr, &localLen);

	InetAddress localAddrObj(localAddr);
	InetAddress peerAddrObj(peerAddr);

	LOG_INFO("TcpServer::newConnection - new connection {} from {}:{}",
			 connName,
			 peerAddrObj.toIp(),
			 peerAddrObj.port());

	auto conn = std::make_shared<TcpConnection>(m_loop, connName, connfd, localAddrObj, peerAddrObj);
	m_connections[connName] = conn;

	conn->setConnectionCallback(m_connectionCallback);
	conn->setMessageCallback(m_messageCallback);
	conn->setCloseCallback([this](const std::shared_ptr<TcpConnection>& c) { removeConnection(c); });

	m_loop->queueInLoop([conn]() { conn->connectEstablished(); });
}

void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn)
{
	LOG_INFO("TcpServer::removeConnection - connection {} closed", conn->name());
	m_connections.erase(conn->name());
	m_loop->queueInLoop([conn]() { conn->connectDestroyed(); });
}
