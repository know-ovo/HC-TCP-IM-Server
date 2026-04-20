#include "base/config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

Config& Config::instance()
{
    static Config instance;
    return instance;
}

void Config::trim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

bool Config::load(const std::string& filePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.clear();

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return false;
    }

    std::string currentSection;
    std::string line;

    while (std::getline(file, line))
    {
        trim(line);

        if (line.empty() || line[0] == '#' || line[0] == ';')
        {
            continue;
        }

        if (line[0] == '[' && line.back() == ']')
        {
            currentSection = line.substr(1, line.size() - 2);
            trim(currentSection);
            continue;
        }

        size_t pos = line.find('=');
        if (pos != std::string::npos)
        {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            trim(key);
            trim(value);
            if (!currentSection.empty() && !key.empty())
            {
                m_data[currentSection][key] = value;
            }
        }
    }

    file.close();
    return true;
}

std::string Config::getString(const std::string& section, const std::string& key, const std::string& defaultValue)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto sectionIt = m_data.find(section);
    if (sectionIt != m_data.end())
    {
        auto keyIt = sectionIt->second.find(key);
        if (keyIt != sectionIt->second.end())
        {
            return keyIt->second;
        }
    }
    return defaultValue;
}

int Config::getInt(const std::string& section, const std::string& key, int defaultValue)
{
    std::string value = getString(section, key, "");
    if (!value.empty())
    {
        try
        {
            return std::stoi(value);
        }
        catch (...) {}
    }
    return defaultValue;
}

bool Config::getBool(const std::string& section, const std::string& key, bool defaultValue)
{
    std::string value = getString(section, key, "");
    if (value == "true" || value == "1" || value == "yes" || value == "on")
    {
        return true;
    }
    else if (value == "false" || value == "0" || value == "no" || value == "off")
    {
        return false;
    }
    return defaultValue;
}

double Config::getDouble(const std::string& section, const std::string& key, double defaultValue)
{
    std::string value = getString(section, key, "");
    if (!value.empty())
    {
        try
        {
            return std::stod(value);
        }
        catch (...) {}
    }
    return defaultValue;
}
