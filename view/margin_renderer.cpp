#include "margin_renderer.hpp"
#include <QFontMetricsF>
#include <QFont>
#include <cmath>

namespace nashville::view
{

static constexpr const char* k_flat  = "\u266D"; // ♭
static constexpr const char* k_sharp = "\u266F"; // ♯

void margin_renderer::paint(QPainter& painter,
                            const QRectF& margin_rect,
                            const model::song& song)
{
    painter.save();

    QFont base_font("Georgia", 13);
    QFont key_font("Georgia", 16, QFont::Bold);
    QFontMetricsF base_fm(base_font);
    QFontMetricsF key_fm(key_font);

    qreal cx = margin_rect.center().x();
    qreal y  = margin_rect.top() + k_top_padding;

    // ----------------------------------------------------------------
    // 1. Key — letter (+ accidental) inside a circle
    // ----------------------------------------------------------------
    QString key_letter, key_accidental;
    parse_key(song.key(), key_letter, key_accidental);
    QString key_display = key_letter + key_accidental;

    painter.setFont(key_font);
    qreal key_text_w = key_fm.horizontalAdvance(key_display);
    qreal key_text_h = key_fm.height();
    qreal circle_r  = std::max(key_text_w, key_text_h) / 2.0 + k_circle_padding;

    QPointF circle_center(cx, y + circle_r);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 1.5));
    painter.drawEllipse(circle_center, circle_r, circle_r);

    qreal key_x        = circle_center.x() - key_text_w / 2.0;
    qreal key_baseline = circle_center.y()
                        + (key_fm.ascent() - key_fm.descent()) / 2.0;
    painter.drawText(QPointF(key_x, key_baseline), key_display);

    y += circle_r * 2.0 + k_element_spacing;

    // ----------------------------------------------------------------
    // 2. Time signature — stacked numerals, centered
    // ----------------------------------------------------------------
    const auto& ts = song.time_sig();
    QString count_str = QString::number(ts.count());
    QString kind_str  = QString::number(static_cast<int>(ts.kind()));

    painter.setFont(base_font);
    qreal ts_num_w = std::max(base_fm.horizontalAdvance(count_str),
                              base_fm.horizontalAdvance(kind_str));

    qreal count_x = cx - base_fm.horizontalAdvance(count_str) / 2.0;
    qreal kind_x  = cx - base_fm.horizontalAdvance(kind_str)  / 2.0;

    qreal row_h    = base_fm.ascent() + base_fm.descent();  // full glyph height
    qreal sep      = 3.0;                                  // gap below glyph and above denominator
    qreal sep_left = cx - ts_num_w / 2.0;

    painter.drawText(QPointF(count_x, y + base_fm.ascent()), count_str);

    // Separator line — centred, 1px thick, with breathing room above and below
    qreal line_y = std::round(y + row_h + sep);
    painter.fillRect(QRectF(sep_left, line_y, ts_num_w, 1.0), Qt::black);

    painter.drawText(QPointF(kind_x, line_y + sep + base_fm.ascent()), kind_str);

    y += row_h + sep + 1.0 + sep + row_h + k_element_spacing;

    // ----------------------------------------------------------------
    // 3. Tempo — note glyph + " = " + BPM, centered
    // ----------------------------------------------------------------
    auto [bpm, beat_unit] = song.tempo();
    QString tempo_str = tempo_glyph(beat_unit) + " = " + QString::number(bpm);

    painter.setFont(base_font);
    qreal tempo_x = cx - base_fm.horizontalAdvance(tempo_str) / 2.0;
    painter.drawText(QPointF(tempo_x, y + base_fm.ascent()), tempo_str);

    painter.restore();
}

// ---------------------------------------------------------------------------
// parse_key
// ---------------------------------------------------------------------------
void margin_renderer::parse_key(const std::string& key,
                               QString& letter,
                               QString& accidental)
{
    if (key.empty())
    {
        letter     = "?";
        accidental = "";
        return;
    }

    letter     = QString(QChar(key[0])).toUpper();
    accidental = "";

    if (key.size() > 1)
    {
        char acc = key[1];
        if (acc == 'b' || acc == 'B')
            accidental = QString(k_flat);
        else if (acc == '#')
            accidental = QString(k_sharp);
    }
}

// ---------------------------------------------------------------------------
// tempo_glyph
// ---------------------------------------------------------------------------
QString margin_renderer::tempo_glyph(model::chord::time beat_unit)
{
    switch (beat_unit)
    {
        case model::chord::time::QUARTER:
        case model::chord::time::DOTTED_QUARTER:
            return "\u2669"; // ♩
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:
            return "\u266A"; // ♪
        case model::chord::time::HALF:
        case model::chord::time::DOTTED_HALF:
            return "\u2609"; // ☉ (half note)
        case model::chord::time::WHOLE:
            return "\u25CB"; // ○ (whole note)
        default:
            return "\u2669";
    }
}

} // namespace nashville::view
