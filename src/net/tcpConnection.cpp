// TcpConnection.cpp
#include "net/tcpConnection.h"
#include "net/eventLoop.h"
#include "net/channel.h"
#include "base/log.h"
#include <errno.h>
#include <string.h>

TcpConnection::TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                               const InetAddress& localAddr, const InetAddress& peerAddr)
	: m_loop(loop)
	, m_name(name)
	, m_state(State::kConnecting)
	, m_sockfd(sockfd)
	, m_channel(new Channel(loop, sockfd))
	, m_localAddr(localAddr)
	, m_peerAddr(peerAddr)
	, m_highWaterMark(1024 * 1024)
	, m_lowWaterMark(256 * 1024)
	, m_maxOutputBufferBytes(4 * 1024 * 1024)
	, m_backPressured(false)
{
	m_channel->setReadCallback([this](Timestamp t) { handleRead(t); });
	m_channel->setWriteCallback([this]() { handleWrite(); });
	m_channel->setCloseCallback([this]() { handleClose(); });
	m_channel->setErrorCallback([this]() { handleError(); });
}

TcpConnection::~TcpConnection()
{
	if (m_state == State::kConnected)
	{
#ifdef _WIN32
		closesocket(m_sockfd);
#else
		close(m_sockfd);
#endif
	}
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
	m_loop->assertInLoopThread();
	bool receivedData = false;
	while (true)
	{
		int savedErrno = 0;
		ssize_t n = m_inputBuffer.readFd(m_sockfd, &savedErrno);
		if (n > 0)
		{
			receivedData = true;
			continue;
		}
		if (n == 0)
		{
			handleClose();
			return;
		}
		errno = savedErrno;
		if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK)
		{
			break;
		}
		LOG_ERROR("TcpConnection::handleRead error");
		handleError();
		return;
	}

	if (receivedData && m_messageCallback)
	{
		m_messageCallback(shared_from_this(), &m_inputBuffer, receiveTime);
	}
}

void TcpConnection::handleWrite()
{
	m_loop->assertInLoopThread();
	if (m_channel->isWriting())
	{
		while (m_outputBuffer.readableBytes() > 0)
		{
			ssize_t n = 0;
#ifdef _WIN32
			n = ::send(m_sockfd, m_outputBuffer.peek(), static_cast<int>(m_outputBuffer.readableBytes()), 0);
#else
			n = ::write(m_sockfd, m_outputBuffer.peek(), m_outputBuffer.readableBytes());
#endif
			if (n > 0)
			{
				m_outputBuffer.retrieve(n);
				if (m_backPressured && m_outputBuffer.readableBytes() <= m_lowWaterMark)
				{
					m_backPressured = false;
					LOG_INFO("TcpConnection::handleWrite backpressure recovered: conn={}, buffer={}",
							 m_name, m_outputBuffer.readableBytes());
				}
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				break;
			}
			LOG_ERROR("TcpConnection::handleWrite error");
			break;
		}
		if (m_outputBuffer.readableBytes() == 0)
		{
			m_channel->disableWriting();
			if (m_writeCompleteCallback)
			{
				m_writeCompleteCallback(shared_from_this());
			}
			if (m_state == State::kDisconnecting)
			{
				shutdownInLoop();
			}
		}
	}
}

void TcpConnection::handleClose()
{
	m_loop->assertInLoopThread();
	LOG_INFO("TcpConnection::handleClose state = {}", static_cast<int>(m_state));
	if (m_state == State::kConnected || m_state == State::kDisconnecting)
	{
		setState(State::kDisconnected);
		m_channel->disableAll();
		if (m_connectionCallback)
		{
			m_connectionCallback(shared_from_this());
		}
		if (m_closeCallback)
		{
			m_closeCallback(shared_from_this());
		}
	}
}

void TcpConnection::handleError()
{
	int err = 0;
	socklen_t optlen = sizeof(err);
#ifdef _WIN32
	int ret = getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &optlen);
#else
	int ret = getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, &err, &optlen);
#endif
	if (ret < 0)
	{
		err = errno;
	}
	LOG_ERROR("TcpConnection::handleError [{}] - SO_ERROR = {}", m_name, err);
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
	m_loop->assertInLoopThread();
	ssize_t nwrote = 0;
	size_t remaining = len;
	bool faultError = false;
	if (m_state == State::kDisconnected)
	{
		LOG_WARN("disconnected, give up writing");
		return;
	}
	if (!m_channel->isWriting() && m_outputBuffer.readableBytes() == 0)
	{
#ifdef _WIN32
		nwrote = ::send(m_sockfd, static_cast<const char*>(data), static_cast<int>(len), 0);
#else
		nwrote = ::write(m_sockfd, data, len);
#endif
		if (nwrote >= 0)
		{
			remaining = len - nwrote;
			if (remaining == 0 && m_writeCompleteCallback)
			{
				m_writeCompleteCallback(shared_from_this());
			}
		}
		else
		{
			nwrote = 0;
			if (errno != EWOULDBLOCK)
			{
				LOG_ERROR("TcpConnection::sendInLoop error");
				if (errno == EPIPE || errno == ECONNRESET)
				{
					faultError = true;
				}
			}
		}
	}
	if (!faultError && remaining > 0)
	{
		const size_t pendingBytes = m_outputBuffer.readableBytes() + remaining;
		if (pendingBytes > m_maxOutputBufferBytes)
		{
			LOG_WARN("TcpConnection::sendInLoop output buffer exceeded limit: conn={}, pending={}, max={}",
					 m_name, pendingBytes, m_maxOutputBufferBytes);
			m_backPressured = true;
			return;
		}

		m_outputBuffer.append(static_cast<const char*>(data) + nwrote, remaining);
		if (!m_backPressured && m_outputBuffer.readableBytes() >= m_highWaterMark)
		{
			m_backPressured = true;
			LOG_WARN("TcpConnection::sendInLoop high water mark reached: conn={}, buffer={}, high={}",
					 m_name, m_outputBuffer.readableBytes(), m_highWaterMark);
			if (m_highWaterMarkCallback)
			{
				m_highWaterMarkCallback(shared_from_this(), m_outputBuffer.readableBytes());
			}
		}
		if (!m_channel->isWriting())
		{
			m_channel->enableWriting();
		}
	}
}

void TcpConnection::shutdownInLoop()
{
	m_loop->assertInLoopThread();
	if (!m_channel->isWriting())
	{
#ifdef _WIN32
		::shutdown(m_sockfd, SD_SEND);
#else
		::shutdown(m_sockfd, SHUT_WR);
#endif
	}
}

void TcpConnection::forceCloseInLoop()
{
	m_loop->assertInLoopThread();
	if (m_state == State::kConnected || m_state == State::kDisconnecting)
	{
		handleClose();
	}
}

void TcpConnection::send(const void* data, size_t len)
{
	if (m_state == State::kConnected)
	{
		std::string message(static_cast<const char*>(data), len);
		if (m_loop->isInLoopThread())
		{
			sendInLoop(message.data(), message.size());
		}
		else
		{
			auto self = shared_from_this();
			m_loop->queueInLoop([self, message = std::move(message)]() {
				self->sendInLoop(message.data(), message.size());
			});
		}
	}
}

void TcpConnection::send(const std::string& data)
{
	send(data.data(), data.size());
}

void TcpConnection::send(Buffer* buffer)
{
	if (m_state == State::kConnected)
	{
		if (m_loop->isInLoopThread())
		{
			sendInLoop(buffer->peek(), buffer->readableBytes());
			buffer->retrieveAll();
		}
		else
		{
			void (TcpConnection::*fp)(const std::string&) = &TcpConnection::send;
			m_loop->queueInLoop(std::bind(fp, this, buffer->retrieveAllAsString()));
		}
	}
}

void TcpConnection::shutdown()
{
	if (m_state == State::kConnected)
	{
		setState(State::kDisconnecting);
		m_loop->runInLoop([this]() { shutdownInLoop(); });
	}
}

void TcpConnection::forceClose()
{
	if (m_state == State::kConnected || m_state == State::kDisconnecting)
	{
		setState(State::kDisconnecting);
		m_loop->queueInLoop([this]() { forceCloseInLoop(); });
	}
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
	if (m_state == State::kConnected || m_state == State::kDisconnecting)
	{
		setState(State::kDisconnecting);
		m_loop->queueInLoop([this, seconds]() {
			forceCloseInLoop();
		});
	}
}

void TcpConnection::connectEstablished()
{
	m_loop->assertInLoopThread();
	if (m_state == State::kConnecting)
	{
		setState(State::kConnected);
		m_channel->enableReading();
		if (m_connectionCallback)
		{
			m_connectionCallback(shared_from_this());
		}
	}
}

void TcpConnection::connectDestroyed()
{
	m_loop->assertInLoopThread();
	if (m_state == State::kConnected)
	{
		setState(State::kDisconnected);
		m_channel->disableAll();
		if (m_connectionCallback)
		{
			m_connectionCallback(shared_from_this());
		}
	}
	m_channel->remove();
}