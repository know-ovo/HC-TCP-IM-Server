#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#endif

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

#include "base/log.h"
#include "codec/codec.h"
#include "codec/protocol.h"
#include "codec/buffer.h"
#include "codec/messageSerializer.h"

using namespace std;
using namespace chrono;

atomic<long long> g_totalMessages(0);
atomic<long long> g_totalErrors(0);
atomic<long long> g_totalBytes(0);
atomic<long long> g_totalTimeouts(0);
atomic<bool> g_running(true);

struct LatencyStats
{
	vector<double> latencies;
	std::mutex m_mutex;

	void addLatency(double latencyMs)
	{
		lock_guard<std::mutex> lock(m_mutex);
		latencies.push_back(latencyMs);
	}

	double getPercentile(double percentile)
	{
		lock_guard<std::mutex> lock(m_mutex);
		if (latencies.empty())
			return 0.0;
		vector<double> sorted = latencies;
		sort(sorted.begin(), sorted.end());
		size_t index = static_cast<size_t>(sorted.size() * percentile / 100.0);
		if (index >= sorted.size())
			index = sorted.size() - 1;
		return sorted[index];
	}

	double getAverage()
	{
		lock_guard<std::mutex> lock(m_mutex);
		if (latencies.empty())
			return 0.0;
		double sum = 0.0;
		for (double l : latencies)
			sum += l;
		return sum / latencies.size();
	}

	double getMax()
	{
		lock_guard<std::mutex> lock(m_mutex);
		if (latencies.empty())
			return 0.0;
		return *max_element(latencies.begin(), latencies.end());
	}

	double getMin()
	{
		lock_guard<std::mutex> lock(m_mutex);
		if (latencies.empty())
			return 0.0;
		return *min_element(latencies.begin(), latencies.end());
	}

	size_t getCount()
	{
		lock_guard<std::mutex> lock(m_mutex);
		return latencies.size();
	}
};

LatencyStats g_latencyStats;
std::mutex g_consoleMutex;

#ifdef _WIN32
void InitWinsock()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		LOG_ERROR("WSAStartup failed");
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

void ClientThread(int clientId, const string& serverIp, int port, int durationSec, int intervalUs, int readTimeoutMs)
{
#ifdef _WIN32
	int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
#endif

	if (sockfd < 0)
	{
		g_totalErrors++;
		return;
	}

	if (!SetNonBlocking(sockfd))
	{
		g_totalErrors++;
#ifdef _WIN32
		closesocket(sockfd);
#else
		close(sockfd);
#endif
		return;
	}

	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(static_cast<uint16_t>(port));
	inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

	int connectResult = connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
	
#ifdef _WIN32
	if (connectResult == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
#else
	if (connectResult < 0 && errno != EINPROGRESS)
#endif
	{
		g_totalErrors++;
#ifdef _WIN32
		closesocket(sockfd);
#else
		close(sockfd);
#endif
		return;
	}

	if (!WaitForWrite(sockfd, 5000))
	{
		g_totalErrors++;
#ifdef _WIN32
		closesocket(sockfd);
#else
		close(sockfd);
#endif
		return;
	}

	auto startTime = steady_clock::now();
	auto endTime = startTime + seconds(durationSec);

	while (g_running && steady_clock::now() < endTime)
	{
		auto sendStart = steady_clock::now();

		protocol::HeartbeatReq heartbeatReq;
		string msg = serializer::Serialize(heartbeatReq);

		Buffer buffer;
		Codec::pack(&buffer, protocol::CmdHeartbeatReq, static_cast<uint32_t>(clientId), msg);

		bool sendSuccess = false;
		size_t totalSent = 0;
		const char* data = buffer.peek();
		size_t remaining = buffer.readableBytes();

		while (remaining > 0 && g_running)
		{
			if (!WaitForWrite(sockfd, 1000))
			{
				break;
			}

#ifdef _WIN32
			ssize_t n = send(sockfd, data + totalSent, static_cast<int>(remaining), 0);
#else
			ssize_t n = write(sockfd, data + totalSent, remaining);
#endif

			if (n > 0)
			{
				totalSent += n;
				remaining -= n;
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
					break;
				}
			}
		}

		if (remaining == 0)
		{
			sendSuccess = true;
			g_totalMessages++;
			g_totalBytes += totalSent;
		}

		if (sendSuccess)
		{
			if (WaitForRead(sockfd, readTimeoutMs))
			{
				char readBuf[4096];
#ifdef _WIN32
				ssize_t bytesRead = recv(sockfd, readBuf, sizeof(readBuf), 0);
#else
				ssize_t bytesRead = read(sockfd, readBuf, sizeof(readBuf));
#endif

				auto sendEnd = steady_clock::now();
				double latencyMs = duration_cast<microseconds>(sendEnd - sendStart).count() / 1000.0;
				g_latencyStats.addLatency(latencyMs);

				if (bytesRead <= 0)
				{
					g_totalErrors++;
					break;
				}
			}
			else
			{
				g_totalTimeouts++;
				auto sendEnd = steady_clock::now();
				double latencyMs = duration_cast<microseconds>(sendEnd - sendStart).count() / 1000.0;
				g_latencyStats.addLatency(latencyMs);
			}
		}
		else
		{
			g_totalErrors++;
			break;
		}

		if (intervalUs > 0)
		{
#ifdef _WIN32
			Sleep(intervalUs / 1000);
#else
			usleep(intervalUs);
#endif
		}
	}

#ifdef _WIN32
	closesocket(sockfd);
#else
	close(sockfd);
#endif
}

void ProgressMonitor(int durationSec)
{
	auto startTime = steady_clock::now();
	auto endTime = startTime + seconds(durationSec);

	while (g_running && steady_clock::now() < endTime)
	{
		auto now = steady_clock::now();
		int elapsed = static_cast<int>(duration_cast<seconds>(now - startTime).count());

		long long total = g_totalMessages.load();
		long long errors = g_totalErrors.load();
		long long timeouts = g_totalTimeouts.load();

		double currentQps = 0;
		if (elapsed > 0)
		{
			currentQps = static_cast<double>(total) / elapsed;
		}

		{
			lock_guard<std::mutex> lock(g_consoleMutex);
			cout << "\r[" << elapsed << "s/" << durationSec << "s] "
				 << "Msgs: " << total
				 << " | Errs: " << errors
				 << " | Timeouts: " << timeouts
				 << " | QPS: " << fixed << setprecision(0) << currentQps
				 << "  " << flush;
		}

		this_thread::sleep_for(seconds(1));
	}

	cout << endl;
}

void GenerateReport(const string& reportFile,
					int numClients,
					int durationSec,
					double elapsed,
					int intervalUs,
					int readTimeoutMs)
{
	ofstream report(reportFile);
	if (!report.is_open())
	{
		LOG_ERROR("Failed to open report file: {}", reportFile);
		return;
	}

	long long total = g_totalMessages.load();
	long long errors = g_totalErrors.load();
	long long timeouts = g_totalTimeouts.load();
	long long bytes = g_totalBytes.load();
	double qps = total / elapsed;

	report << "========================================\n";
	report << "       TCP Server Performance Report\n";
	report << "========================================\n\n";

	report << "## Test Configuration\n";
	report << "| Parameter       | Value          |\n";
	report << "|-----------------|----------------|\n";
	report << "| Concurrent Clients | " << numClients << " |\n";
	report << "| Test Duration   | " << durationSec << " seconds |\n";
	report << "| Request Interval| " << intervalUs << " μs |\n";
	report << "| Read Timeout    | " << readTimeoutMs << " ms |\n\n";

	report << "## Throughput Metrics\n";
	report << "| Metric          | Value          |\n";
	report << "|-----------------|----------------|\n";
	report << "| Total Messages  | " << total << " |\n";
	report << "| Total Errors    | " << errors << " |\n";
	report << "| Total Timeouts  | " << timeouts << " |\n";
	report << "| Error Rate      | " << fixed << setprecision(2) << (total > 0 ? (double)errors / total * 100 : 0) << "% |\n";
	report << "| Timeout Rate    | " << fixed << setprecision(2) << (total > 0 ? (double)timeouts / total * 100 : 0) << "% |\n";
	report << "| Elapsed Time    | " << fixed << setprecision(2) << elapsed << " s |\n";
	report << "| QPS             | " << fixed << setprecision(2) << qps << " |\n";
	report << "| Throughput      | " << fixed << setprecision(2) << (bytes / 1024.0 / elapsed) << " KB/s |\n\n";

	report << "## Latency Metrics (ms)\n";
	report << "| Metric          | Value          |\n";
	report << "|-----------------|----------------|\n";
	report << "| Min Latency     | " << fixed << setprecision(3) << g_latencyStats.getMin() << " |\n";
	report << "| Max Latency     | " << fixed << setprecision(3) << g_latencyStats.getMax() << " |\n";
	report << "| Avg Latency     | " << fixed << setprecision(3) << g_latencyStats.getAverage() << " |\n";
	report << "| P50 Latency     | " << fixed << setprecision(3) << g_latencyStats.getPercentile(50) << " |\n";
	report << "| P90 Latency     | " << fixed << setprecision(3) << g_latencyStats.getPercentile(90) << " |\n";
	report << "| P95 Latency     | " << fixed << setprecision(3) << g_latencyStats.getPercentile(95) << " |\n";
	report << "| P99 Latency     | " << fixed << setprecision(3) << g_latencyStats.getPercentile(99) << " |\n\n";

	auto getRating = [](double qps) -> string {
		if (qps >= 50000) return "Excellent";
		if (qps >= 30000) return "Good";
		if (qps >= 10000) return "Fair";
		return "Poor";
	};

	auto getLatencyRating = [](double latency) -> string {
		if (latency <= 5) return "Excellent";
		if (latency <= 10) return "Good";
		if (latency <= 50) return "Fair";
		return "Poor";
	};

	report << "## Performance Rating\n";
	report << "| Metric          | Value          | Rating |\n";
	report << "|-----------------|----------------|--------|\n";
	report << "| QPS             | " << fixed << setprecision(0) << qps << " | " << getRating(qps) << " |\n";
	report << "| Avg Latency     | " << fixed << setprecision(3) << g_latencyStats.getAverage() << " ms | " << getLatencyRating(g_latencyStats.getAverage()) << " |\n";
	report << "| P99 Latency     | " << fixed << setprecision(3) << g_latencyStats.getPercentile(99) << " ms | " << getLatencyRating(g_latencyStats.getPercentile(99)) << " |\n";
	report << "| Error Rate      | " << fixed << setprecision(2) << (total > 0 ? (double)errors / total * 100 : 0) << "% | " << (errors == 0 ? "Excellent" : (errors < total / 100 ? "Good" : "Poor")) << " |\n\n";

	report << "========================================\n";
	time_t now = time(nullptr);
	report << "Report generated at: " << ctime(&now);
	report.close();

	LOG_INFO("Report saved to: {}", reportFile);
}

void PrintUsage(const char* progName)
{
	cout << "Usage: " << progName << " [options]\n";
	cout << "Options:\n";
	cout << "  -h <host>       Server IP address (default: 127.0.0.1)\n";
	cout << "  -p <port>       Server port (default: 8888)\n";
	cout << "  -c <clients>    Number of concurrent clients (default: 100)\n";
	cout << "  -d <duration>   Test duration in seconds (default: 60)\n";
	cout << "  -i <interval>   Request interval in microseconds (default: 1000)\n";
	cout << "  -t <timeout>    Read timeout in milliseconds (default: 100)\n";
	cout << "  -o <file>       Output report file (default: report.md)\n";
	cout << "  --help          Show this help message\n";
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
	InitWinsock();
#endif

	Log::init("benchmark", "logs/benchmark.log", spdlog::level::info);

	string serverIp = "127.0.0.1";
	int port = 8888;
	int numClients = 100;
	int durationSec = 60;
	int intervalUs = 1000;
	int readTimeoutMs = 100;
	string reportFile = "report.md";

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
		else if (arg == "-c" && i + 1 < argc)
		{
			numClients = atoi(argv[++i]);
		}
		else if (arg == "-d" && i + 1 < argc)
		{
			durationSec = atoi(argv[++i]);
		}
		else if (arg == "-i" && i + 1 < argc)
		{
			intervalUs = atoi(argv[++i]);
		}
		else if (arg == "-t" && i + 1 < argc)
		{
			readTimeoutMs = atoi(argv[++i]);
		}
		else if (arg == "-o" && i + 1 < argc)
		{
			reportFile = argv[++i];
		}
		else if (arg == "--help")
		{
			PrintUsage(argv[0]);
			return 0;
		}
	}

	LOG_INFO("========================================");
	LOG_INFO("Starting benchmark: {} clients, {} seconds", numClients, durationSec);
	LOG_INFO("Server: {}:{}", serverIp, port);
	LOG_INFO("Read timeout: {} ms", readTimeoutMs);
	LOG_INFO("========================================");

	vector<thread> threads;
	threads.reserve(numClients + 1);

	thread monitorThread(ProgressMonitor, durationSec);

	auto startTime = steady_clock::now();

	for (int i = 0; i < numClients; ++i)
	{
		threads.emplace_back(ClientThread, i, serverIp, port, durationSec, intervalUs, readTimeoutMs);
	}

	for (auto& t : threads)
	{
		if (t.joinable())
		{
			t.join();
		}
	}

	g_running = false;
	if (monitorThread.joinable())
	{
		monitorThread.join();
	}

	auto endTime = steady_clock::now();
	double elapsed = duration_cast<duration<double>>(endTime - startTime).count();

	LOG_INFO("========================================");
	LOG_INFO("Benchmark completed");
	LOG_INFO("========================================");
	LOG_INFO("Total messages: {}", g_totalMessages.load());
	LOG_INFO("Total errors: {}", g_totalErrors.load());
	LOG_INFO("Total timeouts: {}", g_totalTimeouts.load());
	LOG_INFO("Elapsed time: {:.2f} seconds", elapsed);
	LOG_INFO("QPS: {:.2f}", g_totalMessages.load() / elapsed);
	LOG_INFO("========================================");

	GenerateReport(reportFile, numClients, durationSec, elapsed, intervalUs, readTimeoutMs);

#ifdef _WIN32
	CleanupWinsock();
#endif

	return 0;
}
