#include "app.hpp"
#include <QCoreApplication>
#include <QStandardPaths>
#include <filesystem>
#include "loggable.hpp"
#include "spdlog/cfg/env.h"

int main(int argc, char* argv[])
{
#if defined(Q_OS_LINUX)
    // Force XCB whenever a Wayland compositor is present.  Keying off
    // WAYLAND_DISPLAY rather than XDG_SESSION_TYPE matches what Qt's own
    // platform auto-detection actually looks at, so this can't disagree
    // with Qt about whether Wayland is available.
    //
    // Unconditional, deliberately: QT_QPA_PLATFORM is not reliably empty
    // just because nothing here has set it.  Distros commonly export it as
    // a fallback LIST, e.g. "wayland;xcb" — meaning "try wayland, fall back
    // to xcb only if wayland fails to INITIALIZE."  A previous version of
    // this check only overrode an EMPTY QT_QPA_PLATFORM, on the reasoning
    // that a non-empty value meant someone had made an explicit choice to
    // respect.  In practice a distro-supplied fallback list isn't that kind
    // of choice, and Wayland almost always initializes successfully even
    // when it later crashes at runtime (see the QWaylandScreen SIGSEGV this
    // guard exists for) — so the fallback to xcb never triggered, and the
    // Wayland plugin ran every time regardless of this code.  Overwriting
    // the variable outright, rather than only filling it in when absent, is
    // what actually forces xcb in that case.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
#endif
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
