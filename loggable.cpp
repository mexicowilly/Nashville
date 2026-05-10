#include "loggable.hpp"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"
#include <QStandardPaths>
#include <filesystem>

namespace
{

std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;

}

namespace nashville
{

loggable::loggable(const std::string& name)
{
    if (sinks.empty())
    {
        #if !defined(TESTING)
        std::filesystem::path data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString();
        sinks.emplace_back(std::make_shared<spdlog::sinks::rotating_file_sink_st>(data_dir / "Nashville.log",
                                                                                  1048576,
                                                                                  10));
        #endif
        sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_sink_st>());
    }
    lgr_ = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    lgr_->set_pattern(PATTERN);
    lgr_->set_level(spdlog::get_level());
}

}
