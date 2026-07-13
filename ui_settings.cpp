#include "ui_settings.hpp"

#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QLocale>
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

nashville::page_size page_size()
{
    QSettings s(settings_file(), QSettings::IniFormat);
    const QString v = s.value(PAGE_SIZE_KEY, "letter").toString();
    if (v == "a4")
        return nashville::page_size::a4;
    // Anything else (including "letter" and any unrecognized/corrupt
    // value) falls back to the default rather than throwing — a garbled
    // settings.ini shouldn't prevent the app from opening a chart.
    return nashville::page_size::letter;
}

void set_page_size(nashville::page_size sz)
{
    QSettings s(settings_file(), QSettings::IniFormat);
    s.setValue(PAGE_SIZE_KEY, sz == nashville::page_size::a4 ? "a4" : "letter");
    s.sync();
}

nashville::page_margins margins()
{
    QSettings s(settings_file(), QSettings::IniFormat);
    auto read_one = [&](const char* key) {
        bool ok = false;
        const double v = s.value(key, MARGIN_DEFAULT_PT).toDouble(&ok);
        if (!ok)
            return MARGIN_DEFAULT_PT;
        return std::clamp(v, MARGIN_MIN_PT, MARGIN_MAX_PT);
    };
    nashville::page_margins m;
    m.top    = read_one(MARGIN_TOP_KEY);
    m.bottom = read_one(MARGIN_BOTTOM_KEY);
    m.left   = read_one(MARGIN_LEFT_KEY);
    m.right  = read_one(MARGIN_RIGHT_KEY);
    return m;
}

void set_margins(const nashville::page_margins& m)
{
    QSettings s(settings_file(), QSettings::IniFormat);
    s.setValue(MARGIN_TOP_KEY,    std::clamp(m.top,    MARGIN_MIN_PT, MARGIN_MAX_PT));
    s.setValue(MARGIN_BOTTOM_KEY, std::clamp(m.bottom, MARGIN_MIN_PT, MARGIN_MAX_PT));
    s.setValue(MARGIN_LEFT_KEY,   std::clamp(m.left,   MARGIN_MIN_PT, MARGIN_MAX_PT));
    s.setValue(MARGIN_RIGHT_KEY,  std::clamp(m.right,  MARGIN_MIN_PT, MARGIN_MAX_PT));
    s.sync();
}

length_unit_pref length_unit_preference()
{
    QSettings s(settings_file(), QSettings::IniFormat);
    const QString v = s.value(LENGTH_UNIT_KEY, "auto").toString();
    if (v == "in") return length_unit_pref::inches;
    if (v == "cm") return length_unit_pref::centimeters;
    return length_unit_pref::auto_detect;
}

void set_length_unit_preference(length_unit_pref pref)
{
    QSettings s(settings_file(), QSettings::IniFormat);
    const char* v = "auto";
    if (pref == length_unit_pref::inches)      v = "in";
    if (pref == length_unit_pref::centimeters) v = "cm";
    s.setValue(LENGTH_UNIT_KEY, v);
    s.sync();
}

nashville::length_unit effective_length_unit()
{
    const auto pref = length_unit_preference();
    if (pref == length_unit_pref::inches)
        return nashville::length_unit::inches;
    if (pref == length_unit_pref::centimeters)
        return nashville::length_unit::centimeters;

    // Auto: MeasurementSystem is Imperial pretty much only for the US (and
    // a couple of territories); everyone else defaults to Metric.
    return QLocale::system().measurementSystem() == QLocale::MetricSystem
         ? nashville::length_unit::centimeters
         : nashville::length_unit::inches;
}

} // namespace nashville::ui_settings
