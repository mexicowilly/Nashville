#include "ui_settings.hpp"

#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <algorithm>

namespace nashville::ui_settings
{

namespace
{
// Absolute path to the settings INI under AppConfigLocation.  main() has
// already create_directories'd this directory on startup, so QSettings can
// write here without further setup.
QString settings_file()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + "/settings.ini";
}
} // namespace

double font_scale()
{
    QSettings s(settings_file(), QSettings::IniFormat);
    bool ok = false;
    const double v = s.value(FONT_SCALE_KEY, FONT_SCALE_DEFAULT).toDouble(&ok);
    if (!ok)
        return FONT_SCALE_DEFAULT;
    return std::clamp(v, FONT_SCALE_MIN, FONT_SCALE_MAX);
}

void set_font_scale(double scale)
{
    QSettings s(settings_file(), QSettings::IniFormat);
    s.setValue(FONT_SCALE_KEY, std::clamp(scale, FONT_SCALE_MIN, FONT_SCALE_MAX));
    s.sync();
}

} // namespace nashville::ui_settings
