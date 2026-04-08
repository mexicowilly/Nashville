#include "song_body_widget.hpp"
#include "margin_renderer.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QApplication>
#include <cmath>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
song_body_widget::song_body_widget(const model::song& song, QWidget* parent)
    : QWidget(parent), song_(song)
{
    setMouseTracking(true);
    init_fonts();
    rebuild();
}

void song_body_widget::init_fonts()
{
    int bravura_id = QFontDatabase::addApplicationFont(":/fonts/Bravura.otf");
    QString music_family = (bravura_id != -1)
                          ? QFontDatabase::applicationFontFamilies(bravura_id).first()
                          : QApplication::font().family();

    fonts_.number = QFont("Georgia", 18, QFont::Normal);
    fonts_.modifier     = QFont("Georgia", 11);
    fonts_.articulation = QFont("Georgia", 12);
    fonts_.music        = QFont(music_family, 14);
}

// ---------------------------------------------------------------------------
// rebuild
// ---------------------------------------------------------------------------
void song_body_widget::rebuild()
{
    QRectF content_rect(margin_width_ + k_content_padding,
                       k_content_padding,
                       std::max(0.0, width()  - margin_width_ - k_content_padding * 2),
                       std::max(0.0, height() - k_content_padding * 2));
    compute_layout(content_rect);
    update();
}

// ---------------------------------------------------------------------------
// compute_layout
// ---------------------------------------------------------------------------
void song_body_widget::compute_layout(const QRectF& content_rect)
{
    lines_.clear();

    if (song_.empty())
        return;

    const auto& bars       = song_.bars();
    const unsigned bpl_pref = song_.bars_per_line();

    // --- Pass 1: group bars into lines ---
    // Rules:
    //   - is_eol_ forces a break after this bar
    //   - extends_line_ suppresses the count-break for this bar
    //   - otherwise break when count_in_line reaches bpl_pref

    struct raw_line { std::vector<const model::bar*> bars; };
    std::vector<raw_line> raw_lines;
    raw_line current;
    unsigned count_in_line = 0;

    for (const auto& b : bars)
    {
        current.bars.push_back(&b);
        count_in_line++;

        bool force_break = b.is_eol();
        bool count_break = (count_in_line >= bpl_pref) && !b.extends_line();

        if (force_break || count_break)
        {
            raw_lines.push_back(std::move(current));
            current.bars.clear();
            count_in_line = 0;
        }
    }
    if (!current.bars.empty())
        raw_lines.push_back(std::move(current));

    qreal plain_h     = plain_bar_height();
    qreal duration_h  = duration_bar_height();
    qreal sec_label_h = section_label_height();

    // --- Pass 2: compute per-column widths ---
    // Column index = bar position within its line (0-based).
    // Every bar in the same column gets the same width = max natural width in that column.
    constexpr qreal k_bar_padding = 16.0;
    std::vector<qreal> col_widths;  // indexed by column (position within line)
    for (const auto& raw : raw_lines)
    {
        bool line_is_dur = false;
        for (const auto* b : raw.bars)
            if (!b->empty() && b->chords().front().duration().has_value())
                { line_is_dur = true; break; }
        qreal bar_h = line_is_dur ? duration_h : plain_h;

        for (std::size_t j = 0; j < raw.bars.size(); ++j)
        {
            qreal w = bar_renderer::width_hint(*raw.bars[j], bar_h, fonts_) + k_bar_padding;
            if (j >= col_widths.size())
                col_widths.push_back(w);
            else
                col_widths[j] = std::max(col_widths[j], w);
        }
    }

    // --- Pass 3: compute geometry ---
    qreal y = content_rect.top();

    // Use the larger bar height for spacing calculations to ensure consistent
    // vertical spacing between lines, even when duration mode varies within a line.
    qreal uniform_bar_h = std::max(plain_h, duration_h);

    for (std::size_t line_idx = 0; line_idx < raw_lines.size(); ++line_idx)
    {
        const auto& raw = raw_lines[line_idx];
        line_layout line;

        // Determine line-level flags
        for (const auto* b : raw_lines[line_idx].bars)
        {
            if (!b->empty() && b->chords().front().duration().has_value())
                line.is_duration_mode = true;
            if (b->section() && !line.section_label)
                line.section_label = QString::fromStdString(*b->section());
        }

        qreal actual_bar_h = line.is_duration_mode ? duration_h : plain_h;
        qreal line_top = y + (line.section_label ? sec_label_h : 0.0);

        qreal x = content_rect.left();
        for (std::size_t j = 0; j < raw_lines[line_idx].bars.size(); ++j)
        {
            const model::bar* b = raw_lines[line_idx].bars[j];
            qreal bar_w = col_widths[j];

            bar_layout bl;
            bl.bar           = b;
            bl.rect          = QRectF(x, line_top, bar_w, actual_bar_h);
            bl.is_duration_mode = !b->empty()
                                && b->chords().front().duration().has_value();

            // Continuation dot: appears after the last "normal count" bar,
            // i.e. bar at index (bpl_pref - 1) when the next bar extends the line.
            if (j == bpl_pref - 1
                && (j + 1) < raw_lines[line_idx].bars.size()
                && raw_lines[line_idx].bars[j + 1]->extends_line())
            {
                bl.show_continuation_dot = true;
            }

            line.bars.push_back(bl);
            x += bar_w + k_inter_bar_spacing;
        }

        qreal line_h = actual_bar_h + (line.section_label ? sec_label_h : 0.0);
        line.rect   = QRectF(content_rect.left(), y, content_rect.width(), line_h);
        lines_.push_back(std::move(line));

        y = line_top + actual_bar_h + k_line_spacing;
    }

    setMinimumHeight(static_cast<int>(y + k_content_padding));
}

// ---------------------------------------------------------------------------
// Height helpers
// ---------------------------------------------------------------------------
qreal song_body_widget::plain_bar_height() const
{
    QFontMetricsF fm(fonts_.number);
    return fm.height() / chord_renderer::k_number_zone_ratio;
}

qreal song_body_widget::duration_bar_height() const
{
    QFontMetricsF music_fm(fonts_.music);
    // Add a compact fixed rhythm row on top of plain bar height.
    // Avoids inflated music font metrics (Bravura fm.height() is ~76px at 14pt).
    constexpr qreal k_rhythm_row_px = 16.0;
    return plain_bar_height() + k_rhythm_row_px + bar_renderer::k_rule_thickness;
}

qreal song_body_widget::section_label_height() const
{
    QFontMetricsF fm(fonts_.modifier);
    return fm.height() + 6.0;
}

// ---------------------------------------------------------------------------
// paintEvent
// ---------------------------------------------------------------------------
void song_body_widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    paint_margin(painter, QRectF(0, 0, margin_width_, height()));
    paint_divider(painter);

    for (const auto& line : lines_)
        paint_line(painter, line);
}

// ---------------------------------------------------------------------------
// paint_margin
// ---------------------------------------------------------------------------
void song_body_widget::paint_margin(QPainter& painter, const QRectF& margin_rect) const
{
    margin_renderer::paint(painter, margin_rect, song_);
}

// ---------------------------------------------------------------------------
// paint_divider
// ---------------------------------------------------------------------------
void song_body_widget::paint_divider(QPainter& painter) const
{
    painter.save();
    painter.setPen(QPen(QColor(180, 180, 180), 1));
    painter.drawLine(margin_width_, 0, margin_width_, height());
    painter.restore();
}

// ---------------------------------------------------------------------------
// paint_line
// ---------------------------------------------------------------------------
void song_body_widget::paint_line(QPainter& painter, const line_layout& line) const
{
    if (line.section_label)
        paint_section_label(painter, *line.section_label, line.rect);

    for (const auto& bl : line.bars)
    {
        bar_renderer::paint(painter, bl.rect, *bl.bar, fonts_, line.is_duration_mode);

        if (bl.show_continuation_dot)
            paint_continuation_dot(painter, bl.rect);
    }
}

// ---------------------------------------------------------------------------
// paint_section_label
// ---------------------------------------------------------------------------
void song_body_widget::paint_section_label(QPainter& painter,
                                        const QString& label,
                                        const QRectF& line_rect) const
{
    painter.save();

    QFont label_font = fonts_.modifier;
    label_font.setBold(true);
    label_font.setItalic(true);
    painter.setFont(label_font);
    QFontMetricsF fm(label_font);

    qreal label_y = line_rect.top() + fm.ascent();
    painter.drawText(QPointF(line_rect.left(), label_y), label);

    qreal rule_y = line_rect.top() + fm.height() + 2.0;
    painter.setPen(QPen(QColor(180, 180, 180), 0.75));
    painter.drawLine(QPointF(line_rect.left(),  rule_y),
                     QPointF(line_rect.right(), rule_y));

    painter.restore();
}

// ---------------------------------------------------------------------------
// paint_continuation_dot
// ---------------------------------------------------------------------------
void song_body_widget::paint_continuation_dot(QPainter& painter,
                                           const QRectF& preceding_bar_rect) const
{
    painter.save();
    constexpr qreal dot_r = 3.0;
    qreal cx = preceding_bar_rect.right() + k_inter_bar_spacing / 2.0;
    qreal cy = preceding_bar_rect.center().y();
    painter.setBrush(Qt::black);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(cx, cy), dot_r, dot_r);
    painter.restore();
}

// ---------------------------------------------------------------------------
// resizeEvent
// ---------------------------------------------------------------------------
void song_body_widget::resizeEvent(QResizeEvent*)
{
    rebuild();
}

// ---------------------------------------------------------------------------
// Draggable divider
// ---------------------------------------------------------------------------
bool song_body_widget::near_divider(int x) const
{
    return std::abs(x - margin_width_) <= k_divider_hit_width;
}

void song_body_widget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && near_divider(event->pos().x()))
    {
        dragging_divider_ = true;
        drag_start_x_      = event->pos().x();
        drag_start_margin_ = margin_width_;
        setCursor(Qt::SplitHCursor);
    }
}

void song_body_widget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_divider_)
    {
        int delta     = event->pos().x() - drag_start_x_;
        int new_margin = qBound(k_min_margin_width,
                               drag_start_margin_ + delta,
                               k_max_margin_width);
        if (new_margin != margin_width_)
        {
            margin_width_ = new_margin;
            rebuild();
        }
    }
    else
    {
        setCursor(near_divider(event->pos().x()) ? Qt::SplitHCursor
                                                : Qt::ArrowCursor);
    }
}

void song_body_widget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_divider_)
    {
        dragging_divider_ = false;
        setCursor(near_divider(event->pos().x()) ? Qt::SplitHCursor
                                                : Qt::ArrowCursor);
    }
}

// ---------------------------------------------------------------------------
// paint_to_rect — for printing
// ---------------------------------------------------------------------------
void song_body_widget::paint_to_rect(QPainter& painter, const QRectF& page_rect) const
{
    painter.save();

    qreal scale_x = page_rect.width()  / static_cast<qreal>(width());
    qreal scale_y = page_rect.height() / static_cast<qreal>(height());
    qreal scale  = std::min(scale_x, scale_y);

    painter.translate(page_rect.left(), page_rect.top());
    painter.scale(scale, scale);

    QRectF my_rect(0, 0, width(), height());
    painter.fillRect(my_rect, Qt::white);
    margin_renderer::paint(painter, QRectF(0, 0, margin_width_, height()), song_);
    paint_divider(painter);
    for (const auto& line : lines_)
        paint_line(painter, line);

    painter.restore();
}

} // namespace nashville::view
