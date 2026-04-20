// Buffer.cpp

#include "codec/buffer.h"
#include "base/log.h"
#include <errno.h>

#ifndef _WIN32
#include <sys/uio.h>
#endif

ssize_t Buffer::readFd(int fd, int* savedErrno)
{
	char extrabuf[65536];
	size_t writable = writableBytes();

#ifdef _WIN32
	char* bufPtr = beginWrite();
	int n = recv(fd, bufPtr, static_cast<int>(writable), 0);
	if (n < 0)
	{
		*savedErrno = WSAGetLastError();
		LOG_ERROR("Buffer::readFd - recv error: %d", *savedErrno);
	}
	else if (static_cast<size_t>(n) <= writable)
	{
		m_writerIndex += n;
	}
	else
	{
		m_writerIndex = m_buffer.size();
		append(extrabuf, n - writable);
	}
#else
	struct iovec vec[2];
	vec[0].iov_base = beginWrite();
	vec[0].iov_len = writable;
	vec[1].iov_base = extrabuf;
	vec[1].iov_len = sizeof(extrabuf);

	ssize_t n = readv(fd, vec, 2);
	if (n < 0)
	{
		*savedErrno = errno;
		LOG_ERROR("Buffer::readFd - readv error: %d", errno);
	}
	else if (static_cast<size_t>(n) <= writable)
	{
		m_writerIndex += n;
	}
	else
	{
		m_writerIndex = m_buffer.size();
		append(extrabuf, n - writable);
	}
#endif

	return n;
}

void Buffer::makeSpace(size_t len)
{
	if (writableBytes() + prependableBytes() < len + kCheapPrepend)
	{
		m_buffer.resize(m_writerIndex + len);
	}
	else
	{
		size_t readable = readableBytes();
		std::copy(begin() + m_readerIndex, begin() + m_writerIndex, begin() + kCheapPrepend);
		m_readerIndex = kCheapPrepend;
		m_writerIndex = m_readerIndex + readable;
		assert(readable == readableBytes());
	}
}
