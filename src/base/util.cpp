// Util.cpp
#include "base/util.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <ctime>

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

namespace util {

uint16_t HostToNetwork16(uint16_t host16)
{
	return htons(host16);
}

uint16_t NetworkToHost16(uint16_t net16)
{
	return ntohs(net16);
}

uint32_t HostToNetwork32(uint32_t host32)
{
	return htonl(host32);
}

uint32_t NetworkToHost32(uint32_t net32)
{
	return ntohl(net32);
}

uint64_t HostToNetwork64(uint64_t host64)
{
#ifdef _WIN32
	return ((uint64_t)htonl(host64 & 0xFFFFFFFF) << 32) | htonl(host64 >> 32);
#else
#if __BYTE_ORDER == __BIG_ENDIAN
	return host64;
#else
	return ((uint64_t)htonl(host64 & 0xFFFFFFFF) << 32) | htonl(host64 >> 32);
#endif
#endif
}

uint64_t NetworkToHost64(uint64_t net64)
{
#ifdef _WIN32
	return ((uint64_t)ntohl(net64 & 0xFFFFFFFF) << 32) | ntohl(net64 >> 32);
#else
#if __BYTE_ORDER == __BIG_ENDIAN
	return net64;
#else
	return ((uint64_t)ntohl(net64 & 0xFFFFFFFF) << 32) | ntohl(net64 >> 32);
#endif
#endif
}

int64_t GetTimestampMs()
{
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	uint64_t time = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	return (int64_t)((time - 116444736000000000LL) / 10000);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

int64_t GetTimestampUs()
{
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	uint64_t time = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	return (int64_t)((time - 116444736000000000LL) / 10);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

std::string GetTimestampString()
{
	int64_t ms = GetTimestampMs();
	time_t sec = ms / 1000;
	int usec = (ms % 1000) * 1000;

#ifdef _WIN32
	struct tm tm;
	struct tm* ptm = localtime(&sec);
	if (ptm)
	{
		tm = *ptm;
		char buf[64];
		snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%06d",
				 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				 tm.tm_hour, tm.tm_min, tm.tm_sec, usec);
		return buf;
	}
	return "";
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm tm;
	localtime_r(&tv.tv_sec, &tm);
	char buf[64];
	snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%06ld",
			 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			 tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);
	return buf;
#endif
}

void SleepMs(int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep(ms * 1000);
#endif
}

std::vector<std::string> SplitString(const std::string& str, char delimiter)
{
	std::vector<std::string> result;
	std::stringstream ss(str);
	std::string token;
	while (std::getline(ss, token, delimiter))
	{
		result.push_back(token);
	}
	return result;
}

std::string TrimString(const std::string& str)
{
	size_t start = str.find_first_not_of(" \t\n\r");
	size_t end = str.find_last_not_of(" \t\n\r");
	if (start == std::string::npos)
	{
		return "";
	}
	return str.substr(start, end - start + 1);
}

std::string ToLower(const std::string& str)
{
	std::string result = str;
	std::transform(result.begin(), result.end(), result.begin(),
				   [](unsigned char c) { return std::tolower(c); });
	return result;
}

std::string ToUpper(const std::string& str)
{
	std::string result = str;
	std::transform(result.begin(), result.end(), result.begin(),
				   [](unsigned char c) { return std::toupper(c); });
	return result;
}

bool StartWith(const std::string& str, const std::string& prefix)
{
	if (str.length() < prefix.length())
	{
		return false;
	}
	return str.compare(0, prefix.length(), prefix) == 0;
}

bool EndWith(const std::string& str, const std::string& suffix)
{
	if (str.length() < suffix.length())
	{
		return false;
	}
	return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

} // namespace util
