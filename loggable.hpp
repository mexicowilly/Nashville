#pragma once

#include "spdlog/spdlog.h"

namespace nashville
{

class loggable
{
public:
    static constexpr const char* PATTERN = "%Y-%m-%d %H:%M:%S.%f %L %n: %v";

    loggable(const std::string& name);

    std::shared_ptr<spdlog::logger> lgr();

private:
    std::shared_ptr<spdlog::logger> lgr_;
};

inline std::shared_ptr<spdlog::logger> loggable::lgr()
{
    return lgr_;
}

}
