#include "app.hpp"
#include <QCoreApplication>
#include <QStandardPaths>
#include <fstream>
#include <filesystem>
#include <chucho/configuration.hpp>
#include <iostream>

namespace
{

void configure_chucho()
{
    std::filesystem::path cfg_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString();
    std::filesystem::create_directories(cfg_dir);
    std::filesystem::path data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString();
    std::filesystem::create_directories(data_dir);
    std::cout << "Using config path: " << cfg_dir << std::endl;
    std::cout << "Using data path: " << data_dir << std::endl;
    std::filesystem::path cfg_file = cfg_dir / "chucho.yaml";
    if (!std::filesystem::exists(cfg_file))
    {
        std::string chucho_config = R"(
- chucho::logger:
      name: <root>
      level: info
      chucho::rolling_file_writer:
          file_name: DATA_DIR/Nashville.log
          chucho::pattern_formatter:
              pattern: '%D{%Y-%m-%d %H:%M:%S.%Q} %-5p %c{1}: %m%n'
          chucho::numbered_file_roller:
              max_index: 10
          chucho::size_file_roll_trigger:
              max_size: 1MB)";
        chucho_config.replace(chucho_config.find("DATA_DIR"), 8, data_dir.string());
        auto lvl = std::getenv("CHUCHO_LEVEL");
        if (lvl != nullptr)
            chucho_config.replace(chucho_config.find("info"), 4, lvl);
        std::ofstream cfg_out(cfg_file);
        cfg_out << chucho_config;
        cfg_out.close();
    }
    chucho::configuration::set_file_name(cfg_file.string());
}

}

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName("WillMason");
    QCoreApplication::setApplicationName("Nashville");
    configure_chucho();
    nashville::app app(argc, argv);
    return app.run();
}
