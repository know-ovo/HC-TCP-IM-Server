#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace infra
{

class MetricsRegistry
{
public:
    static MetricsRegistry& instance();

    void setEnabled(bool enabled);
    bool enabled() const;

    void incCounter(const std::string& name,
                    double delta = 1.0,
                    const std::string& help = "");
    void observe(const std::string& name,
                 double value,
                 const std::string& help = "");
    void setGauge(const std::string& name,
                  double value,
                  const std::string& help = "");

    std::string renderPrometheus() const;

private:
    struct CounterMetric
    {
        double value = 0.0;
        std::string help;
    };

    struct GaugeMetric
    {
        double value = 0.0;
        std::string help;
    };

    struct HistogramMetric
    {
        std::vector<double> bounds;
        std::vector<uint64_t> bucketCounts;
        double sum = 0.0;
        uint64_t count = 0;
        std::string help;
    };

    MetricsRegistry() = default;

    static std::string sanitizeName(const std::string& name);
    static std::string sanitizeHelp(const std::string& help);

    mutable std::mutex m_mutex;
    bool m_enabled = false;
    std::unordered_map<std::string, CounterMetric> m_counters;
    std::unordered_map<std::string, GaugeMetric> m_gauges;
    std::unordered_map<std::string, HistogramMetric> m_histograms;
};

} // namespace infra
