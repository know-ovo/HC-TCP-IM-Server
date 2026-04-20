#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "base/log.h"

inline void Require(bool condition, const char* expression, const char* file, int line)
{
    if (condition)
    {
        return;
    }

    std::ostringstream oss;
    oss << file << ":" << line << " assertion failed: " << expression;
    throw std::runtime_error(oss.str());
}

#define REQUIRE(expr) Require((expr), #expr, __FILE__, __LINE__)

inline int RunTestMain(const std::function<void()>& body)
{
    try
    {
        Log::init("tests", "logs/tests.log", spdlog::level::warn);
        body();
        std::cout << "[PASS]" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[FAIL] " << ex.what() << std::endl;
        return 1;
    }
}
