#pragma once

#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <hicr/core/definitions.hpp>

namespace deployr 
{

__INLINE__ std::string getCurrentDateTime()
{
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    auto str = oss.str();

    return str;
}

} // namespace deployr