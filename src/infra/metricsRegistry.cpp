#include "infra/metricsRegistry.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace infra
{

namespace
{

std::vector<double> DefaultHistogramBounds()
{
    return { 1.0, 5.0, 10.0, 50.0, 100.0, 250.0, 500.0, 1000.0, 5000.0 };
}

} // namespace

MetricsRegistry& MetricsRegistry::instance()
{
    static MetricsRegistry registry;
    return registry;
}

void MetricsRegistry::setEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
}

bool MetricsRegistry::enabled() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_enabled;
}

void MetricsRegistry::incCounter(const std::string& name, double delta, const std::string& help)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled)
    {
        return;
    }

    CounterMetric& metric = m_counters[sanitizeName(name)];
    metric.value += delta;
    if (!help.empty())
    {
        metric.help = help;
    }
}

void MetricsRegistry::observe(const std::string& name, double value, const std::string& help)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled)
    {
        return;
    }

    HistogramMetric& metric = m_histograms[sanitizeName(name)];
    if (metric.bounds.empty())
    {
        metric.bounds = DefaultHistogramBounds();
        metric.bucketCounts.assign(metric.bounds.size() + 1, 0);
    }
    if (!help.empty())
    {
        metric.help = help;
    }

    metric.sum += value;
    ++metric.count;

    size_t bucketIndex = metric.bounds.size();
    for (size_t i = 0; i < metric.bounds.size(); ++i)
    {
        if (value <= metric.bounds[i])
        {
            bucketIndex = i;
            break;
        }
    }
    ++metric.bucketCounts[bucketIndex];
}

void MetricsRegistry::setGauge(const std::string& name, double value, const std::string& help)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled)
    {
        return;
    }

    GaugeMetric& metric = m_gauges[sanitizeName(name)];
    metric.value = value;
    if (!help.empty())
    {
        metric.help = help;
    }
}

std::string MetricsRegistry::renderPrometheus() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream oss;
    for (const auto& item : m_counters)
    {
        oss << "# HELP " << item.first << " " << sanitizeHelp(item.second.help) << "\n";
        oss << "# TYPE " << item.first << " counter\n";
        oss << item.first << " " << item.second.value << "\n";
    }

    for (const auto& item : m_gauges)
    {
        oss << "# HELP " << item.first << " " << sanitizeHelp(item.second.help) << "\n";
        oss << "# TYPE " << item.first << " gauge\n";
        oss << item.first << " " << item.second.value << "\n";
    }

    for (const auto& item : m_histograms)
    {
        const HistogramMetric& metric = item.second;
        oss << "# HELP " << item.first << " " << sanitizeHelp(metric.help) << "\n";
        oss << "# TYPE " << item.first << " histogram\n";

        uint64_t cumulative = 0;
        for (size_t i = 0; i < metric.bounds.size(); ++i)
        {
            cumulative += metric.bucketCounts[i];
            oss << item.first << "_bucket{le=\"" << metric.bounds[i] << "\"} " << cumulative << "\n";
        }

        cumulative += metric.bucketCounts.back();
        oss << item.first << "_bucket{le=\"+Inf\"} " << cumulative << "\n";
        oss << item.first << "_sum " << metric.sum << "\n";
        oss << item.first << "_count " << metric.count << "\n";
    }

    return oss.str();
}

std::string MetricsRegistry::sanitizeName(const std::string& name)
{
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char ch : name)
    {
        const bool valid = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') ||
                           ch == '_' || ch == ':';
        sanitized.push_back(valid ? ch : '_');
    }

    if (sanitized.empty())
    {
        return "unnamed_metric";
    }
    return sanitized;
}

std::string MetricsRegistry::sanitizeHelp(const std::string& help)
{
    if (help.empty())
    {
        return "no_help";
    }

    std::string sanitized = help;
    std::replace(sanitized.begin(), sanitized.end(), '\n', ' ');
    return sanitized;
}

} // namespace infra
