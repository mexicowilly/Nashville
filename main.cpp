#include "app.hpp"
#include <QCoreApplication>
#include <QStandardPaths>
#include <fstream>
#include <filesystem>
#include "loggable.hpp"
#include "spdlog/cfg/env.h"

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName("WillMason");
    QCoreApplication::setApplicationName("Nashville");
    std::filesystem::path cfg_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString();
    std::filesystem::create_directories(cfg_dir);
    std::filesystem::path data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString();
    std::filesystem::create_directories(data_dir);
    spdlog::cfg::load_env_levels(nashville::loggable::ENV_VARIABLE);
    spdlog::set_pattern(nashville::loggable::PATTERN);
    spdlog::info("Using config path '{}'", cfg_dir.string());
    spdlog::info("Using data path '{}'", data_dir.string());
    nashville::app app(argc, argv);
    return app.run();
}
