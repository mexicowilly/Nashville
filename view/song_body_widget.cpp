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
    qreal title_h = title_height();
    QRectF content_rect(margin_width_ + k_content_padding,
                       title_h + k_content_padding,
                       std::max(0.0, width()  - margin_width_ - k_content_padding * 2),
                       std::max(0.0, height() - title_h - k_content_padding * 2));
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
    struct raw_line { std::vector<const model::bar*> bars; };
    std::vector<raw_line> raw_lines;
    raw_line current;

    for (const auto& b : bars)
    {
        current.bars.push_back(&b);

        if (b.is_eol())
        {
            raw_lines.push_back(std::move(current));
            current.bars.clear();
        }
    }
    if (!current.bars.empty())
        raw_lines.push_back(std::move(current));

    qreal plain_h_bare = plain_bar_height(false);
    qreal plain_h_art  = plain_bar_height(true);
    qreal duration_h   = duration_bar_height();

    // --- Pass 2: measure section column width ---
    // All lines share the same section column width = widest label + padding.
    // Lines without a label still reserve the column so bars stay aligned.
    constexpr qreal k_label_pad_h = 6.0;   // horizontal padding inside box
    constexpr qreal k_label_pad_v = 3.0;   // vertical padding inside box
    constexpr qreal k_section_gap = 8.0;   // gap between section col and first bar

    QFont label_font = fonts_.modifier;
    label_font.setBold(true);
    QFontMetricsF label_fm(label_font);

    qreal max_label_w = 0.0;
    for (const auto& raw : raw_lines)
        for (const auto* b : raw.bars)
            if (b->section())
                max_label_w = std::max(max_label_w,
                    label_fm.horizontalAdvance(QString::fromStdString(*b->section())));

    // section_col_w is 0 when there are no section labels at all.
    qreal section_col_w = (max_label_w > 0.0)
                          ? max_label_w + k_label_pad_h * 2.0 + k_section_gap
                          : 0.0;

    // Helper: does any chord on this line have an above-number articulation?
    auto line_has_articulation = [](const std::vector<const model::bar*>& bars) {
        for (const auto* b : bars)
            for (const auto& ch : b->chords())
                if (ch.is_pushed() || ch.is_staccato())
                    return true;
        return false;
    };

    auto line_bar_height = [&](const std::vector<const model::bar*>& bars) -> qreal {
        for (const auto* b : bars)
            if (!b->empty() && b->chords().front().duration().has_value())
                return duration_h;
        return line_has_articulation(bars) ? plain_h_art : plain_h_bare;
    };

    // --- Pass 3: compute per-column bar widths ---
    constexpr qreal k_bar_padding = 16.0;
    qreal bars_left = content_rect.left() + section_col_w;
    std::vector<qreal> col_widths;

    for (const auto& raw : raw_lines)
    {
        qreal bar_h = line_bar_height(raw.bars);

        for (std::size_t j = 0; j < raw.bars.size(); ++j)
        {
            qreal w = bar_renderer::width_hint(*raw.bars[j], bar_h, fonts_) + k_bar_padding;
            if (j >= col_widths.size())
                col_widths.push_back(w);
            else
                col_widths[j] = std::max(col_widths[j], w);
        }
    }

    // --- Pass 3.5: determine which lines end a section ---
    // A line ends a section if the next line starts a new section and
    // there are at least 2 sections total.
    auto line_has_section = [&](std::size_t i) {
        for (const auto* b : raw_lines[i].bars)
            if (b->section()) return true;
        return false;
    };
    int section_count = 0;
    for (std::size_t i = 0; i < raw_lines.size(); ++i)
        if (line_has_section(i)) ++section_count;

    std::vector<bool> ends_section(raw_lines.size(), false);
    if (section_count >= 2)
        for (std::size_t i = 0; i + 1 < raw_lines.size(); ++i)
            if (line_has_section(i + 1))
                ends_section[i] = true;

    // --- Pass 4: compute geometry ---
    qreal y = content_rect.top();

    for (std::size_t line_idx = 0; line_idx < raw_lines.size(); ++line_idx)
    {
        const auto& raw = raw_lines[line_idx];
        line_layout line;

        qreal actual_bar_h = line_bar_height(raw.bars);
        line.is_duration_mode = (actual_bar_h == duration_h);
        line.has_articulation = line_has_articulation(raw.bars);

        for (const auto* b : raw.bars)
            if (b->section() && !line.section_label)
                line.section_label = QString::fromStdString(*b->section());

        // Section column: full bar height, left-aligned within content_rect.
        // Width is the label box only (without the gap).
        qreal box_w = section_col_w > 0.0 ? section_col_w - k_section_gap : 0.0;
        // The section label should centre on the number row, not the full bar height.
        // Number row starts at: top_pad(2px) + art_zone (if any) + k_plain_top_pad(4px).
        constexpr qreal k_bar_top_pad   = 2.0;   // matches bar_renderer fixed top_pad
        constexpr qreal k_plain_top_pad = 4.0;   // matches chord_renderer k_plain_top_pad
        qreal art_zone = line.has_articulation
            ? [&]{ QFontMetricsF a(fonts_.articulation); return a.ascent() + a.descent(); }()
            : 0.0;
        qreal num_top = y + k_bar_top_pad + (art_zone > 0.0 ? art_zone : k_plain_top_pad);
        QFontMetricsF num_fm(fonts_.number);
        qreal num_h   = num_fm.ascent() + num_fm.descent();
        line.section_col_rect = QRectF(content_rect.left(), num_top, box_w, num_h);

        qreal x = bars_left;
        for (std::size_t j = 0; j < raw.bars.size(); ++j)
        {
            const model::bar* b = raw.bars[j];
            qreal bar_w = col_widths[j];

            bar_layout bl;
            bl.bar              = b;
            bl.rect             = QRectF(x, y, bar_w, actual_bar_h);
            bl.is_duration_mode = !b->empty()
                                && b->chords().front().duration().has_value();

            bl.num_center_y = num_top + num_h / 2.0;

            if (raw.bars.size() > bpl_pref
                && j == bpl_pref - 1)
            {
                bl.show_continuation_dot = true;
            }

            line.bars.push_back(bl);
            x += bar_w + k_inter_bar_spacing;
        }

        line.draw_section_end_rule = ends_section[line_idx];
        line.rect = QRectF(content_rect.left(), y, content_rect.width(), actual_bar_h);
        lines_.push_back(std::move(line));

        qreal spacing = ends_section[line_idx] ? k_line_spacing : k_line_spacing_normal;
        y += actual_bar_h + spacing;
    }

    setMinimumHeight(static_cast<int>(y + k_content_padding));
}

// ---------------------------------------------------------------------------
// Height helpers
// ---------------------------------------------------------------------------
qreal song_body_widget::plain_bar_height(bool has_articulation) const
{
    QFontMetricsF num_fm(fonts_.number);
    qreal h = num_fm.ascent() + num_fm.descent();
    if (has_articulation)
    {
        QFontMetricsF art_fm(fonts_.articulation);
        h += art_fm.ascent() + art_fm.descent();
    }
    return h;
}

qreal song_body_widget::duration_bar_height() const
{
    constexpr qreal k_rhythm_row_px = 16.0;
    return plain_bar_height() + k_rhythm_row_px + bar_renderer::k_rule_thickness;
}

qreal song_body_widget::title_height() const
{
    QFontMetricsF fm(QFont("Georgia", 16, QFont::Bold));
    return fm.height() + k_title_padding * 2.0;
}

// ---------------------------------------------------------------------------
// paint_title
// ---------------------------------------------------------------------------
void song_body_widget::paint_title(QPainter& painter, qreal widget_width) const
{
    painter.save();

    QFont title_font("Georgia", 16, QFont::Bold);
    painter.setFont(title_font);
    QFontMetricsF fm(title_font);

    QString title = QString::fromStdString(song_.name());
    qreal text_w  = fm.horizontalAdvance(title);
    qreal x       = (widget_width - text_w) / 2.0;
    qreal baseline = k_title_padding + fm.ascent();

    painter.setPen(QPen(Qt::black, 1.0));
    painter.drawText(QPointF(x, baseline), title);

    // Underline directly beneath the text
    qreal underline_y = std::round(baseline + fm.descent() + 1.0);
    painter.drawLine(QPointF(x, underline_y), QPointF(x + text_w, underline_y));

    painter.restore();
}

// ---------------------------------------------------------------------------
// paintEvent
// ---------------------------------------------------------------------------
void song_body_widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    paint_title(painter, width());
    paint_margin(painter, QRectF(0, title_height(), margin_width_, height() - title_height()));
    paint_divider(painter);

    painter.setPen(QPen(Qt::black, 1.0));
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
    qreal y0 = title_height();
    painter.drawLine(QPointF(margin_width_, y0), QPointF(margin_width_, height()));
    painter.restore();
}

// ---------------------------------------------------------------------------
// paint_line
// ---------------------------------------------------------------------------
void song_body_widget::paint_line(QPainter& painter, const line_layout& line) const
{
    if (line.section_label)
        paint_section_label(painter, *line.section_label, line.section_col_rect);

    for (const auto& bl : line.bars)
    {
        bar_renderer::paint(painter, bl.rect, *bl.bar, fonts_, line.is_duration_mode,
                            line.has_articulation);

        if (bl.show_continuation_dot)
            paint_continuation_dot(painter, bl.rect, bl.num_center_y);
    }

    if (line.draw_section_end_rule)
    {
        painter.save();
        qreal rule_y = line.rect.bottom() + k_line_spacing_normal + (k_line_spacing - k_line_spacing_normal) / 2.0;
        painter.setPen(QPen(QColor(180, 180, 180), 1.0));
        painter.drawLine(QPointF(line.rect.left(), rule_y),
                         QPointF(line.rect.right(), rule_y));
        painter.restore();
    }
}

// ---------------------------------------------------------------------------
// paint_section_label
// ---------------------------------------------------------------------------
void song_body_widget::paint_section_label(QPainter& painter,
                                        const QString& label,
                                        const QRectF& col_rect) const
{
    if (col_rect.width() <= 0.0)
        return;

    painter.save();

    QFont label_font = fonts_.modifier;
    label_font.setBold(true);
    painter.setFont(label_font);
    QFontMetricsF fm(label_font);

    constexpr qreal k_pad_h = 6.0;
    constexpr qreal k_pad_v = 3.0;

    qreal box_h = fm.height() + k_pad_v * 2.0;
    qreal box_w = col_rect.width();
    qreal box_y = col_rect.top() + (col_rect.height() - box_h) / 2.0;
    QRectF box(col_rect.left(), box_y, box_w, box_h);

    // Box outline
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 1.0));
    painter.drawRect(box);

    // Label — left-justified with horizontal padding
    qreal text_x = box.left() + k_pad_h;
    qreal text_y = box.top() + k_pad_v + fm.ascent();
    painter.drawText(QPointF(text_x, text_y), label);

    painter.restore();
}

// ---------------------------------------------------------------------------
// paint_continuation_dot
// ---------------------------------------------------------------------------
void song_body_widget::paint_continuation_dot(QPainter& painter,
                                           const QRectF& preceding_bar_rect,
                                           qreal num_center_y) const
{
    painter.save();
    constexpr qreal dot_r = 3.0;
    qreal cx = preceding_bar_rect.right() + k_inter_bar_spacing / 2.0;
    qreal cy = num_center_y;
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
    qreal scale   = std::min(scale_x, scale_y);

    painter.translate(page_rect.left(), page_rect.top());
    painter.scale(scale, scale);

    QRectF my_rect(0, 0, width(), height());
    painter.fillRect(my_rect, Qt::white);
    paint_title(painter, width());
    margin_renderer::paint(painter,
                           QRectF(0, title_height(), margin_width_, height() - title_height()),
                           song_);
    paint_divider(painter);
    painter.setPen(QPen(Qt::black, 1.0));
    for (const auto& line : lines_)
        paint_line(painter, line);

    painter.restore();
}

} // namespace nashville::view
