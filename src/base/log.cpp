#include "base/log.h"

#include <memory>

#include <spdlog/pattern_formatter.h>

#include "infra/traceContext.h"

namespace
{

class TraceContextFlag final : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg&,
                const std::tm&,
                spdlog::memory_buf_t& dest) override
    {
        const std::string context = infra::FormatTraceContextForLog();
        dest.append(context.data(), context.data() + context.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return std::make_unique<TraceContextFlag>();
    }
};

} // namespace

std::shared_ptr<spdlog::logger> Log::s_logger;

void Log::init(const std::string& logName,
               const std::string& logPath,
               int level,
               size_t maxFileSize,
               size_t maxFiles)
{
    try
    {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath, maxFileSize, maxFiles);

        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

        s_logger = std::make_shared<spdlog::logger>(logName, sinks.begin(), sinks.end());

        auto formatter = std::make_unique<spdlog::pattern_formatter>();
        formatter->add_flag<TraceContextFlag>('*')
            .set_pattern("time=%Y-%m-%dT%H:%M:%S.%e level=%l thread=%t %* msg=%v");

        s_logger->set_level(static_cast<spdlog::level::level_enum>(level));
        s_logger->set_formatter(std::move(formatter));

        spdlog::set_default_logger(s_logger);
        spdlog::flush_on(spdlog::level::err);
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        printf("Log init failed: %s\n", ex.what());
    }
}

void Log::setLevel(int level)
{
    if (s_logger)
    {
        s_logger->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

std::shared_ptr<spdlog::logger>& Log::getLogger()
{
    return s_logger;
}
