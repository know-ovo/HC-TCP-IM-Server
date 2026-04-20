#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

#include "base/log.h"
#include "base/threadpool.h"
#include "base/util.h"
#include "codec/buffer.h"
#include "codec/codec.h"
#include "codec/messageSerializer.h"
#include "codec/protocol.h"

namespace
{

using Clock = std::chrono::steady_clock;

template <typename Fn>
double MeasureAverageUs(size_t iterations, Fn&& fn)
{
    const auto begin = Clock::now();
    for (size_t i = 0; i < iterations; ++i)
    {
        fn();
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::micro>(end - begin).count() /
           static_cast<double>(iterations);
}

void PrintResult(const std::string& name, double valueUs)
{
    std::cout << std::left << std::setw(32) << name
              << " avg_us=" << std::fixed << std::setprecision(3) << valueUs
              << std::endl;
}

} // namespace

int main()
{
    Log::init("benchmarks", "logs/benchmarks.log", spdlog::level::warn);

    const size_t iterations = 20000;

    protocol::P2PMsgNotify notify;
    notify.msgId = 1001;
    notify.serverSeq = 2002;
    notify.fromUserId = "alice";
    notify.toUserId = "bob";
    notify.clientMsgId = "client-msg-1";
    notify.content = std::string(256, 'x');
    notify.timestamp = util::GetTimestampMs();

    const double serializeUs = MeasureAverageUs(iterations, [&]() {
        const std::string payload = serializer::Serialize(notify);
        protocol::P2PMsgNotify parsed;
        serializer::Deserialize(payload, parsed);
    });

    const std::string payload = serializer::Serialize(notify);
    const double packUs = MeasureAverageUs(iterations, [&]() {
        Buffer buffer;
        protocol::PacketHeader header;
        header.command = protocol::CmdP2pMsgNotify;
        header.clientSeq = notify.msgId;
        header.serverSeq = notify.serverSeq;
        Codec::pack(&buffer, header, payload);
    });

    const double bufferUs = MeasureAverageUs(iterations, [&]() {
        Buffer buffer;
        buffer.append(payload);
        buffer.retrieveAsString(payload.size() / 2);
        buffer.append(payload);
        buffer.retrieveAll();
    });

    ThreadPool pool(4);
    pool.start();
    const double dispatchUs = MeasureAverageUs(iterations, [&]() {
        auto future = pool.submit([]() { return 42; });
        future.get();
    });
    pool.stop();

    std::cout << "==== Core Benchmark ====" << std::endl;
    PrintResult("serializer_roundtrip", serializeUs);
    PrintResult("codec_pack", packUs);
    PrintResult("buffer_append_recycle", bufferUs);
    PrintResult("threadpool_dispatch", dispatchUs);
    return 0;
}
