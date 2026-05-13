#include "song_body_widget.hpp"
#include "margin_renderer.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFontDatabase>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <stdexcept>
#include <cmath>

namespace nashville::view
{

// Color used for placeholder title text.
static const QColor k_placeholder_color(150, 150, 150);

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
song_body_widget::song_body_widget(model::song& song, QWidget* parent)
    : QWidget(parent), song_(song)
{
    setMouseTracking(true);
    // StrongFocus so the widget receives key events for Esc/clipboard
    // shortcuts.  We also call setFocus() on every mouse press below so
    // that clicking the chart hands keyboard focus back from any sibling
    // widget (toolbar, sidebar) the user may have last interacted with.
    setFocusPolicy(Qt::StrongFocus);
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
    insertion_slots_.clear();

    if (song_.empty())
    {
        // Empty song: the only insertion slot is the would-be first bar at
        // the start of the first line.  No section column is reserved
        // because there are no labels yet.
        compute_insertion_slots(content_rect,
                                /*bars_left=*/content_rect.left(),
                                /*last_line_bottom=*/content_rect.top(),
                                /*last_line_bar_h=*/plain_bar_height(false));
        return;
    }

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

    qreal plain_h_bare   = plain_bar_height(false);
    qreal plain_h_art    = plain_bar_height(true);
    qreal duration_h_bare = duration_bar_height(false);
    qreal duration_h_art  = duration_bar_height(true);

    // --- Pass 2: measure section column width ---
    // All lines share the same section column width = widest label + padding.
    // Lines without a label still reserve the column so bars stay aligned.
    // We always reserve a minimum gutter even when no section labels exist
    // anywhere, so the user can click into the empty space to bootstrap
    // the very first section.  When labels exist, the gutter widens to fit
    // the widest one.
    constexpr qreal k_label_pad_h = 6.0;   // horizontal padding inside box
    constexpr qreal k_label_pad_v = 3.0;   // vertical padding inside box
    constexpr qreal k_section_gap = 8.0;   // gap between section col and first bar
    constexpr qreal k_min_section_col_w = 24.0;  // bootstrap gutter for label-less songs

    QFont label_font = fonts_.modifier;
    label_font.setBold(true);
    QFontMetricsF label_fm(label_font);

    // The UI invariant is "sections live on the first bar of a line."
    // Measure label widths from first-of-line bars only — any stray
    // section on a non-first bar (e.g. from a malformed loaded file) is
    // silently ignored everywhere in the layout, including here.
    qreal max_label_w = 0.0;
    for (const auto& raw : raw_lines)
    {
        if (raw.bars.empty()) continue;
        const auto* first = raw.bars.front();
        if (first->section())
            max_label_w = std::max(max_label_w,
                label_fm.horizontalAdvance(QString::fromStdString(*first->section())));
    }

    qreal labelled_col_w = (max_label_w > 0.0)
                            ? max_label_w + k_label_pad_h * 2.0 + k_section_gap
                            : 0.0;
    qreal section_col_w = std::max(labelled_col_w,
                                   k_min_section_col_w + k_section_gap);

    // Helper: does any chord on this line have an above-number articulation?
    // Ties also live in the articulation zone, so a tied chord forces the
    // zone to be reserved even if nothing on the line is staccato or pushed.
    auto line_has_articulation = [](const std::vector<const model::bar*>& bars) {
        for (const auto* b : bars)
            for (const auto& ch : b->chords())
                if (ch.is_pushed() || ch.is_staccato() || ch.is_tied())
                    return true;
        return false;
    };

    auto line_bar_height = [&](const std::vector<const model::bar*>& bars) -> qreal {
        bool has_art = line_has_articulation(bars);
        for (const auto* b : bars)
            if (!b->empty() && b->chords().front().duration().has_value())
                return has_art ? duration_h_art : duration_h_bare;
        return has_art ? plain_h_art : plain_h_bare;
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
    // there are at least 2 sections total.  Sections live only on the
    // first bar of a line (UI invariant), so the check is a single
    // dereference rather than a scan.
    auto line_has_section = [&](std::size_t i) {
        return !raw_lines[i].bars.empty()
            && raw_lines[i].bars.front()->section().has_value();
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
        line.is_duration_mode = (actual_bar_h == duration_h_bare || actual_bar_h == duration_h_art);
        line.has_articulation = line_has_articulation(raw.bars);

        // Section label comes from the first bar only.  Sections on
        // non-first bars are silently ignored — see the UI invariant.
        if (!raw.bars.empty() && raw.bars.front()->section())
            line.section_label =
                QString::fromStdString(*raw.bars.front()->section());

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

            // Centre the continuation-dot vertically on the chord-number
            // row by asking bar_renderer where that row will paint.  Doing
            // it ourselves with raw font metrics drifts from the renderer
            // (which centres the digit ink in its own numRect), producing
            // a visible misalignment between the dot and the digits.
            bl.num_center_y = bar_renderer::number_row_center_y(
                bl.rect, *b, fonts_,
                line.is_duration_mode, line.has_articulation);

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

    // Insertion slots are derived from the last line's geometry.  The
    // next-line slot's top is the same y the next line would have used had
    // there been one; reusing the loop's trailing spacing means slot
    // placement matches what a freshly-typed bar will look like once
    // re-laid-out.
    const auto& last_line = lines_.back();
    compute_insertion_slots(content_rect,
                            bars_left,
                            /*last_line_bottom=*/last_line.rect.bottom()
                                                  + k_line_spacing_normal,
                            /*last_line_bar_h=*/last_line.rect.height());

    // The next-line slot extends below `y`; grow the minimum height so it
    // stays visible without manual scrolling.
    qreal slots_bottom = y;
    for (const auto& s : insertion_slots_)
        slots_bottom = std::max(slots_bottom, s.rect.bottom());

    setMinimumHeight(static_cast<int>(slots_bottom + k_content_padding));
}

// ---------------------------------------------------------------------------
// compute_insertion_slots
// ---------------------------------------------------------------------------
// The layout for new-bar insertion is intentionally simple: there are at
// most two slots, anchored to the last line (or to the top-left of the
// content area when the song is empty).  This matches the spec's three
// cases under one rule:
//   * Empty song            -> one "first_bar" slot at content top-left.
//   * Last line not full    -> "same_line" after last bar + "next_line".
//   * Last line at capacity -> still "same_line" (extends past bars_per_line
//                              with a continuation dot) + "next_line".
//
// Slot widths are deliberately suggestive rather than precise; the real
// bar width is computed from chord contents after the user types into the
// editor.  Using the last bar's width (or a fixed default when empty)
// produces a visual hint that lines up naturally with the existing bars.
void song_body_widget::compute_insertion_slots(const QRectF& content_rect,
                                               qreal bars_left,
                                               qreal last_line_bottom,
                                               qreal last_line_bar_h)
{
    constexpr qreal k_default_slot_w = 80.0;

    if (song_.empty())
    {
        insertion_slot s;
        s.kind = insertion_slot_kind::first_bar;
        s.rect = QRectF(content_rect.left(), content_rect.top(),
                        k_default_slot_w, last_line_bar_h);
        insertion_slots_.push_back(s);
        return;
    }

    // Slot widths derive from the last bar on the last line — visually it
    // reads as "the next bar will be about this size" while the user
    // hovers, before they've typed anything.
    const auto& last_line = lines_.back();
    qreal slot_w = k_default_slot_w;
    qreal last_bar_right = bars_left;
    if (!last_line.bars.empty())
    {
        const auto& last_bar = last_line.bars.back();
        slot_w = last_bar.rect.width();
        last_bar_right = last_bar.rect.right();
    }

    // same_line: placed immediately after the last bar, separated by the
    // same inter-bar spacing the renderer uses elsewhere.  The bar height
    // matches the last line so the ghost outline aligns with neighbours.
    {
        insertion_slot s;
        s.kind = insertion_slot_kind::same_line;
        s.rect = QRectF(last_bar_right + k_inter_bar_spacing,
                        last_line.rect.top(),
                        slot_w,
                        last_line_bar_h);
        insertion_slots_.push_back(s);
    }

    // next_line: placed at the bar-column left edge, on a fresh row below
    // the last line.  We deliberately keep its width matched to the
    // last_line slot so the two ghosts look like siblings.
    {
        insertion_slot s;
        s.kind = insertion_slot_kind::next_line;
        s.rect = QRectF(bars_left,
                        last_line_bottom,
                        slot_w,
                        last_line_bar_h);
        insertion_slots_.push_back(s);
    }
}

// ---------------------------------------------------------------------------
// paint_insertion_slot
// ---------------------------------------------------------------------------
// Painted with a dashed gray outline.  Only the currently-hovered slot is
// drawn — the others stay invisible to keep the chart uncluttered.
void song_body_widget::paint_insertion_slot(QPainter& painter,
                                            const insertion_slot& slot) const
{
    painter.save();
    QPen pen(k_placeholder_color, 1.0, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(slot.rect);
    painter.restore();
}

int song_body_widget::hit_test_insertion_slot(const QPointF& p) const
{
    for (std::size_t i = 0; i < insertion_slots_.size(); ++i)
        if (insertion_slots_[i].rect.contains(p))
            return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// hit_test_bar
// ---------------------------------------------------------------------------
// Walks the laid-out lines in document order, accumulating an index into
// song_.bars().  When a bar's rect contains the point, the accumulator is
// the bar's position in the song's flat bar vector — which is what the
// edit handler needs to address the bar for mutation.
int song_body_widget::hit_test_bar(const QPointF& p, QRectF* out_rect) const
{
    std::size_t bar_idx = 0;
    for (const auto& line : lines_)
    {
        for (const auto& bl : line.bars)
        {
            if (bl.rect.contains(p))
            {
                if (out_rect) *out_rect = bl.rect;
                return static_cast<int>(bar_idx);
            }
            ++bar_idx;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// hit_test_section_col
// ---------------------------------------------------------------------------
// The section column rect is centred vertically on the bar's number row,
// not on the full bar height — clicking the number row's left edge counts
// as a section click, but clicking the rhythm row below it does not.
// This matches the visual placement of the (existing) label box and keeps
// the click target unambiguous.
int song_body_widget::hit_test_section_col(const QPointF& p) const
{
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].section_col_rect.contains(p))
            return static_cast<int>(i);
    return -1;
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

qreal song_body_widget::duration_bar_height(bool has_articulation) const
{
    QFontMetricsF num_fm(fonts_.number);
    qreal num_h = num_fm.ascent() + num_fm.descent();
    qreal art_h = 0.0;
    if (has_articulation)
    {
        QFontMetricsF art_fm(fonts_.articulation);
        art_h = art_fm.ascent() + art_fm.descent();
    }
    constexpr qreal k_rhythm_row_px = 16.0;
    return num_h + art_h + k_rhythm_row_px + bar_renderer::k_rule_thickness;
}

qreal song_body_widget::title_height() const
{
    QFontMetricsF fm(QFont("Georgia", 16, QFont::Bold));
    return fm.height() + k_title_padding * 2.0;
}

// ---------------------------------------------------------------------------
// paint_title
// ---------------------------------------------------------------------------
void song_body_widget::paint_title(QPainter& painter, qreal widget_width,
                                   bool stash_hit_rect) const
{
    painter.save();

    QFont title_font("Georgia", 16, QFont::Bold);
    painter.setFont(title_font);
    QFontMetricsF fm(title_font);

    QString title = QString::fromStdString(song_.name());
    const QString placeholder = QString::fromUtf8(k_title_placeholder);
    // Treat the title as a placeholder when the model name is empty OR when
    // the displayed text equals the placeholder string ("Title").  Both
    // cases mean the song effectively has no name and should render gray.
    const bool is_placeholder = title.isEmpty() || title == placeholder;

    QString display_title = title.isEmpty() ? placeholder : title;

    qreal text_w  = fm.horizontalAdvance(display_title);
    qreal x       = (widget_width - text_w) / 2.0;
    qreal baseline = k_title_padding + fm.ascent();

    painter.setPen(QPen(is_placeholder ? k_placeholder_color : Qt::black, 1.0));
    painter.drawText(QPointF(x, baseline), display_title);

    // Underline directly beneath the text.  Uses the pen colour set above,
    // so placeholder titles get a gray underline and real titles get black.
    qreal underline_y = std::round(baseline + fm.descent() + 1.0);
    painter.drawLine(QPointF(x, underline_y), QPointF(x + text_w, underline_y));

    if (stash_hit_rect)
    {
        // Slightly padded vertically for easier clicking.  Minimum width
        // ensures the placeholder is clickable even with short text.
        title_rect_ = QRectF(x, k_title_padding - 2.0,
                             std::max(text_w, 40.0),
                             fm.height() + 4.0);
    }

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
    {
        std::size_t first_bar_index = 0;
        for (const auto& line : lines_)
        {
            paint_line(painter, line, first_bar_index);
            first_bar_index += line.bars.size();
        }
    }

    // Only the hovered slot is painted — the others stay invisible until
    // the cursor enters them.  Skipped while an inline editor is open
    // because the editor visually replaces the slot for the duration of
    // the edit, and skipped during divider drag to avoid distracting
    // flicker.
    if (hovered_slot_ >= 0
        && hovered_slot_ < static_cast<int>(insertion_slots_.size())
        && !active_editor_
        && !dragging_divider_)
    {
        paint_insertion_slot(painter, insertion_slots_[hovered_slot_]);
    }

    // Empty section gutter hover: draw a dashed outline matching the
    // would-be label box so the user can see the click target.  Lines
    // that already carry a section label use their painted box as the
    // affordance — no extra outline.
    if (hovered_empty_section_line_ >= 0
        && hovered_empty_section_line_ < static_cast<int>(lines_.size())
        && !active_editor_
        && !dragging_divider_)
    {
        const auto& col_rect = lines_[hovered_empty_section_line_].section_col_rect;
        if (col_rect.width() > 0.0)
        {
            painter.save();
            QPen pen(k_placeholder_color, 1.0, Qt::DashLine);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(col_rect);
            painter.restore();
        }
    }
}

// ---------------------------------------------------------------------------
// paint_margin
// ---------------------------------------------------------------------------
void song_body_widget::paint_margin(QPainter& painter, const QRectF& margin_rect,
                                    bool stash_hit_rects) const
{
    margin_renderer::paint(painter, margin_rect, song_,
                           stash_hit_rects ? &margin_layout_ : nullptr);
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
// The selection background is painted before the bar contents so the
// glyphs stay sharp on top of it.  `first_bar_index` is the flat index
// into song_.bars() of this line's first bar — accumulated by the
// caller as it walks the lines in document order — so we can match each
// bar_layout to its model-level index without re-walking the layout
// from scratch.
void song_body_widget::paint_line(QPainter& painter, const line_layout& line,
                                  std::size_t first_bar_index,
                                  bool show_selection) const
{
    if (line.section_label)
        paint_section_label(painter, *line.section_label, line.section_col_rect);

    std::size_t bar_idx = first_bar_index;
    for (const auto& bl : line.bars)
    {
        if (show_selection && selected_bars_.count(bar_idx))
        {
            // Light gray selection background.  Painted as a filled rect
            // with no border so it reads as a wash behind the glyphs
            // rather than a competing outline.  Chosen mid-light enough
            // to be visible on white but dim enough that black chord
            // numbers and rhythm marks still pop.
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(220, 225, 232));
            painter.drawRect(bl.rect);
            painter.restore();
        }

        bar_renderer::paint(painter, bl.rect, *bl.bar, fonts_, line.is_duration_mode,
                            line.has_articulation);

        if (bl.show_continuation_dot)
            paint_continuation_dot(painter, bl.rect, bl.num_center_y);

        ++bar_idx;
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
// Mouse handling — divider drag, then click-to-edit hit testing
// ---------------------------------------------------------------------------
bool song_body_widget::near_divider(int x) const
{
    return std::abs(x - margin_width_) <= k_divider_hit_width;
}

void song_body_widget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    // Take keyboard focus on every press so subsequent Ctrl+C / Ctrl+V /
    // Esc shortcuts land here rather than on whatever sibling widget last
    // held focus.  Cheap to call even if we already have focus.
    setFocus(Qt::MouseFocusReason);

    // If an inline editor is open and the click lands outside it, commit
    // the current editor before hit-testing the new click.  This lets the
    // user hop straight from one editable element to another.  Clicks
    // inside the editor itself fall through normally so the QLineEdit
    // handles them.
    if (active_editor_ && !active_editor_->geometry().contains(event->pos()))
        close_line_editor(/*commit_value=*/true);

    // Divider drag wins over edit hit-testing.  Starting a drag clears
    // any active bar selection — divider work is unrelated to clipboard
    // ops and a stale selection would be visually confusing.
    if (near_divider(event->pos().x()))
    {
        clear_selection();
        dragging_divider_ = true;
        drag_start_x_      = event->pos().x();
        drag_start_margin_ = margin_width_;
        setCursor(Qt::SplitHCursor);
        return;
    }

    const QPointF p = event->pos();
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool ctrl  = mods.testFlag(Qt::ControlModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);

    // Existing bars: selection-only here.  Editing requires a double-
    // click, which arrives as a separate mouseDoubleClickEvent.  Note
    // that Qt always fires a mousePressEvent before mouseDoubleClickEvent,
    // so the first click of a double-click first selects the bar (giving
    // a momentary highlight) and then the double-click clears that
    // selection and opens the editor — see mouseDoubleClickEvent.
    QRectF bar_rect;
    int bar_idx = hit_test_bar(p, &bar_rect);
    if (bar_idx >= 0)
    {
        auto idx = static_cast<std::size_t>(bar_idx);
        if (shift && selection_anchor_.has_value())
            extend_selection_to(idx);
        else if (ctrl)
            toggle_bar_in_selection(idx);
        else
            select_bar_only(idx);
        return;
    }

    // Any non-bar click without a modifier clears the current selection
    // before processing the click.  We do this even when the click
    // lands on title / margin / section / insertion-slot rects — the
    // user is moving on to a different kind of edit, and a lingering
    // selection from a previous gesture would be a distraction.
    //
    // Modifier-held clicks that miss every bar are a no-op for both
    // selection and edit: the user was reaching for a bar and missed,
    // and yanking their selection out from under them would be hostile.
    if (ctrl || shift)
        return;

    clear_selection();

    if (title_rect_.contains(p))
        edit_title();
    else if (margin_layout_.key_rect.contains(p))
        edit_key();
    else if (margin_layout_.time_sig_rect.contains(p))
        edit_time_signature();
    else if (margin_layout_.tempo_glyph_rect.contains(p))
        edit_tempo_glyph();
    else if (margin_layout_.tempo_bpm_rect.contains(p))
        edit_tempo_bpm();
    else
    {
        // Section column lives to the left of bars; no overlap with bar
        // rects is possible, but checking it before insertion slots
        // keeps the dispatch ordered by "things the user can see and
        // target."
        int sec_line = hit_test_section_col(p);
        if (sec_line >= 0)
        {
            edit_section(static_cast<std::size_t>(sec_line));
            return;
        }
        int slot_idx = hit_test_insertion_slot(p);
        if (slot_idx >= 0)
            edit_new_bar(static_cast<std::size_t>(slot_idx));
    }
}

// Double-click is the gesture for entering bar edit mode.  By the time
// we arrive here, mousePressEvent has already run once (Qt's sequence is
// press → release → doubleClick → release) and may have left the bar
// selected; we clear the selection before opening the editor so the
// inline QLineEdit isn't visually competing with a gray selection wash
// underneath it.
//
// Modifier-held double-clicks aren't a meaningful gesture (Ctrl+double-
// click on a bar would mean "toggle selection AND edit", which is
// contradictory), so we treat them as a plain double-click.
void song_body_widget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const QPointF p = event->pos();

    // Only existing-bar double-clicks are special.  Everywhere else,
    // forward to the press handler so the first-click affordances on
    // title / margin / section / insertion-slot keep working when the
    // user happens to double-click them.
    QRectF bar_rect;
    int bar_idx = hit_test_bar(p, &bar_rect);
    if (bar_idx < 0)
    {
        mousePressEvent(event);
        return;
    }

    // Take focus and (per the contract) clear selection before opening
    // the editor.  edit_bar() will install a QLineEdit on top of the
    // bar; with no selection background, the white editor background
    // sits cleanly over the bar's normal rendering.
    setFocus(Qt::MouseFocusReason);
    clear_selection();

    // If an editor was somehow already open (defensive — open editor +
    // double-click on a different bar would arrive here after the press
    // handler already committed it, but if that path ever changes this
    // keeps the invariant), commit it first.  Use event->pos() (QPoint)
    // here rather than the QPointF `p` above because QWidget::geometry()
    // returns QRect, which has no QPointF::contains overload.
    if (active_editor_ && !active_editor_->geometry().contains(event->pos()))
        close_line_editor(/*commit_value=*/true);

    edit_bar(static_cast<std::size_t>(bar_idx), bar_rect);
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
        return;
    }

    // Helper: drop any outline-bearing hover state and repaint if needed.
    // Used whenever the cursor enters a region where no slot/empty-section
    // outline should be visible.
    auto clear_outline_hover = [&]() {
        bool changed = false;
        if (hovered_slot_ != -1) { hovered_slot_ = -1; changed = true; }
        if (hovered_empty_section_line_ != -1)
            { hovered_empty_section_line_ = -1; changed = true; }
        if (changed) update();
    };

    if (near_divider(event->pos().x()))
    {
        setCursor(Qt::SplitHCursor);
        clear_outline_hover();
        return;
    }

    const QPointF p = event->pos();
    if (title_rect_.contains(p)
        || margin_layout_.key_rect.contains(p)
        || margin_layout_.time_sig_rect.contains(p)
        || margin_layout_.tempo_glyph_rect.contains(p)
        || margin_layout_.tempo_bpm_rect.contains(p))
    {
        setCursor(Qt::PointingHandCursor);
        clear_outline_hover();
        return;
    }

    // Existing bars get the pointing-hand cursor — the rectangle is
    // already visible so no extra outline is needed.
    if (hit_test_bar(p) >= 0)
    {
        setCursor(Qt::PointingHandCursor);
        clear_outline_hover();
        return;
    }

    // Section column hover: cursor changes always; outline is shown only
    // when the line has no section label yet (an existing labelled box
    // is its own affordance and an outline would clash with the box).
    int sec_line = hit_test_section_col(p);
    if (sec_line >= 0)
    {
        setCursor(Qt::PointingHandCursor);
        bool line_has_label = lines_[sec_line].section_label.has_value();
        int new_section_hover = line_has_label ? -1 : sec_line;
        bool changed = false;
        if (hovered_slot_ != -1) { hovered_slot_ = -1; changed = true; }
        if (new_section_hover != hovered_empty_section_line_)
            { hovered_empty_section_line_ = new_section_hover; changed = true; }
        if (changed) update();
        return;
    }

    // Insertion slot hover (last because the slot rects are the lowest-
    // priority click targets).  Repaint only when the hovered slot
    // changes, and drop any stale section-outline hover when we land
    // on a slot.
    int new_slot_hover = hit_test_insertion_slot(p);
    bool changed = false;
    if (new_slot_hover != hovered_slot_)
        { hovered_slot_ = new_slot_hover; changed = true; }
    if (hovered_empty_section_line_ != -1)
        { hovered_empty_section_line_ = -1; changed = true; }
    if (changed) update();

    setCursor(new_slot_hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
// leaveEvent — clear hover outlines when the cursor exits the widget
// ---------------------------------------------------------------------------
// Without this, moving the mouse straight off the widget's edge while
// over a slot or empty-section gutter would leave the dashed outline
// painted indefinitely until the next paint event re-evaluated hover.
void song_body_widget::leaveEvent(QEvent*)
{
    bool changed = false;
    if (hovered_slot_ != -1)               { hovered_slot_ = -1;               changed = true; }
    if (hovered_empty_section_line_ != -1) { hovered_empty_section_line_ = -1; changed = true; }
    if (changed) update();
}

// ---------------------------------------------------------------------------
// keyPressEvent — Esc clears selection, Ctrl+C/X/V drive the clipboard
// ---------------------------------------------------------------------------
// Key events only arrive here when the widget has keyboard focus, which
// we acquire on every mouse press.  When an inline editor is open the
// editor is a child QLineEdit and consumes its own key events (including
// Esc, via the event filter installed in open_line_editor), so the
// shortcuts here can't fire mid-edit — exactly the right behavior.
void song_body_widget::keyPressEvent(QKeyEvent* event)
{
    // Defensive: with the editor open, route everything to the base
    // class.  The editor's own event filter handles its Esc; we don't
    // want a stray "Esc clears selection" running while the user is
    // typing into a bar.
    if (active_editor_)
    {
        QWidget::keyPressEvent(event);
        return;
    }

    const auto mods = event->modifiers();
    const bool ctrl_only = (mods & ~Qt::KeypadModifier) == Qt::ControlModifier;

    if (event->key() == Qt::Key_Escape)
    {
        if (clear_selection())
            return;
        // No selection to clear — let the base class see it so any
        // future global Esc handler can act on it.
        QWidget::keyPressEvent(event);
        return;
    }

    if (ctrl_only && event->key() == Qt::Key_C)
    {
        copy_selection();
        return;
    }
    if (ctrl_only && event->key() == Qt::Key_X)
    {
        cut_selection();
        return;
    }
    if (ctrl_only && event->key() == Qt::Key_V)
    {
        paste_clipboard();
        return;
    }

    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------
bool song_body_widget::clear_selection()
{
    if (selected_bars_.empty() && !selection_anchor_.has_value())
        return false;
    selected_bars_.clear();
    selection_anchor_.reset();
    update();
    return true;
}

void song_body_widget::select_bar_only(std::size_t bar_index)
{
    // Replace the entire selection with just this bar and reset the
    // anchor to it.  Even if the bar was already the sole selected one
    // we still call update() — it's cheap and keeps the code paths
    // uniform; the perf cost of an extra repaint at click time is
    // negligible.
    selected_bars_.clear();
    selected_bars_.insert(bar_index);
    selection_anchor_ = bar_index;
    update();
}

void song_body_widget::toggle_bar_in_selection(std::size_t bar_index)
{
    auto it = selected_bars_.find(bar_index);
    if (it != selected_bars_.end())
    {
        selected_bars_.erase(it);
        // Anchor follows the toggle: if we just removed the anchor, the
        // anchor becomes whatever bar the user most recently *added*,
        // which we approximate as "the highest remaining selected bar"
        // (or none, if the selection is empty).  This keeps a subsequent
        // Shift+click from extending from a phantom anchor on a now-
        // unselected bar.
        if (selection_anchor_ == bar_index)
        {
            if (selected_bars_.empty())
                selection_anchor_.reset();
            else
                selection_anchor_ = *selected_bars_.rbegin();
        }
    }
    else
    {
        selected_bars_.insert(bar_index);
        selection_anchor_ = bar_index;
    }
    update();
}

void song_body_widget::extend_selection_to(std::size_t bar_index)
{
    // Range select replaces the current selection with the inclusive
    // span between the anchor and the clicked bar.  This is the
    // file-manager idiom: Shift+click does NOT add to an existing
    // selection — it picks a new range from the anchor.  Use Ctrl+click
    // (toggle) or Ctrl+Shift+click (not implemented here) for additive
    // range selection if needed later.
    if (!selection_anchor_.has_value())
    {
        // No anchor — fall back to single-bar select so the user isn't
        // stuck with a Shift+click that did nothing.
        select_bar_only(bar_index);
        return;
    }
    std::size_t lo = std::min(*selection_anchor_, bar_index);
    std::size_t hi = std::max(*selection_anchor_, bar_index);
    selected_bars_.clear();
    for (std::size_t i = lo; i <= hi; ++i)
        selected_bars_.insert(i);
    // Anchor stays put — that's how file managers behave: repeated
    // Shift+clicks pivot around the original anchor rather than the
    // last endpoint.
    update();
}

// ---------------------------------------------------------------------------
// Clipboard helpers
// ---------------------------------------------------------------------------
// Bars are stored as to_user_input() strings.  This is the same text
// the inline editor shows, so what the user copies is exactly what
// they'd see if they double-clicked to edit — no hidden state slips
// across the cut/paste boundary.  Pasting goes through
// parse_user_input() into a fresh model::bar, which round-trips
// is_eol, time signatures, and section labels via the model's own
// serialisation rules.
void song_body_widget::copy_selection()
{
    clipboard_.clear();
    if (selected_bars_.empty())
        return;
    const auto& bars = song_.bars();
    for (std::size_t idx : selected_bars_)   // std::set iterates in order
    {
        if (idx < bars.size())
            clipboard_.push_back(bars[idx].to_user_input());
    }
}

void song_body_widget::cut_selection()
{
    if (selected_bars_.empty())
        return;
    copy_selection();
    delete_selection();
}

// Erases every bar whose flat index is in selected_bars_.  We work on a
// copy of the bars vector and reassign in one shot via song::bars(),
// matching the atomic-update pattern used by edit_bar / edit_new_bar.
//
// One semantic gotcha worth being deliberate about: when the deleted
// run includes the song's last bar, the new last bar should not be
// dangling mid-line.  We don't *force* is_eol on the new last bar
// (other Nashville-chart conventions don't require it) but we do leave
// each surviving bar's is_eol exactly as it was — line breaks in the
// non-deleted portions of the song stay where the user put them.
void song_body_widget::delete_selection()
{
    if (selected_bars_.empty())
        return;
    std::vector<model::bar> bars_copy = song_.bars();

    // Erase from the highest index down so each erase doesn't shift
    // the indices we haven't reached yet.  std::set iterates in
    // ascending order, so we walk in reverse.
    for (auto it = selected_bars_.rbegin(); it != selected_bars_.rend(); ++it)
    {
        if (*it < bars_copy.size())
            bars_copy.erase(bars_copy.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    song_.bars(bars_copy);
    selected_bars_.clear();
    selection_anchor_.reset();
    rebuild();
}

// Insert clipboard contents after the highest-indexed selected bar; if
// nothing is selected, append at the end of the song.  After the paste
// the selection is set to the newly inserted bars, so the user can
// immediately cut/copy/delete the pasted run — and Shift+click can
// extend from it.
void song_body_widget::paste_clipboard()
{
    if (clipboard_.empty())
        return;

    std::vector<model::bar> bars_copy = song_.bars();

    // Anchor index where the inserted bars will go (insertion happens
    // *after* this index, so the literal insertion point is anchor+1).
    // size_t for arithmetic, but we treat "no selection" as "append at
    // end" by setting it to bars_copy.size() - 1 and inserting after.
    std::size_t insert_after;
    if (!selected_bars_.empty())
        insert_after = *selected_bars_.rbegin();
    else if (!bars_copy.empty())
        insert_after = bars_copy.size() - 1;
    else
        insert_after = static_cast<std::size_t>(-1);  // empty song: prepend

    // Build the new bars from the clipboard.  Any clipboard entry that
    // fails to parse is silently dropped — paste should never corrupt
    // the song with a half-formed bar, and the clipboard text came
    // from to_user_input() so round-trip failures should be rare.
    std::vector<model::bar> pasted;
    pasted.reserve(clipboard_.size());
    for (const auto& text : clipboard_)
    {
        model::bar staged;
        try
        {
            staged.parse_user_input(text);
        }
        catch (const std::exception&)
        {
            continue;
        }
        pasted.push_back(std::move(staged));
    }
    if (pasted.empty())
        return;

    // Insertion point in the destination vector.
    std::size_t insert_at = (insert_after == static_cast<std::size_t>(-1))
                          ? 0
                          : insert_after + 1;
    bars_copy.insert(bars_copy.begin() + static_cast<std::ptrdiff_t>(insert_at),
                     std::make_move_iterator(pasted.begin()),
                     std::make_move_iterator(pasted.end()));

    song_.bars(bars_copy);

    // Select the newly pasted run so the user sees what just happened
    // and can immediately operate on it.
    selected_bars_.clear();
    for (std::size_t i = 0; i < clipboard_.size(); ++i)
    {
        // clipboard_.size() may exceed pasted.size() if some entries
        // failed to parse above; key the new selection off pasted.size()
        // instead so we don't select a bar that doesn't exist.
        if (i >= pasted.size()) break;
        selected_bars_.insert(insert_at + i);
    }
    selection_anchor_ = insert_at;

    rebuild();
}

// ---------------------------------------------------------------------------
// Edit handlers
// ---------------------------------------------------------------------------

// The title editor's rect is sized to the painted text rect, with a small
// horizontal pad so a long new title has room to grow as it's typed.
// When the title is empty (a fresh song) the editor opens blank with a
// "Title" placeholder hint, matching the gray placeholder shown in the
// painted view.  Empty input on commit is rejected (revert) — both
// because the model layer rejects empty names and because an empty title
// should retain whatever the song was previously called.
void song_body_widget::edit_title()
{
    QRectF r = title_rect_;
    // Give the editor reasonable typing room beyond the current title.
    constexpr qreal k_min_editor_w = 240.0;
    if (r.width() < k_min_editor_w)
    {
        qreal extra = k_min_editor_w - r.width();
        r.adjust(-extra / 2.0, 0, extra / 2.0, 0);
    }
    QString current = QString::fromStdString(song_.name());
    open_line_editor(r, current,
        [this](const QString& text) -> bool
        {
            QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
                return false;  // revert: leave the model name untouched
            song_.name(trimmed.toStdString());
            rebuild();
            return true;
        },
        QString::fromUtf8(k_title_placeholder));
}

void song_body_widget::edit_key()
{
    // The key hit-rect is the full circle.  An editor that exact size and
    // shape would feel cramped — anchor it to the circle's vertical band
    // but stretch across the margin so there's room to type.
    QRectF r = margin_layout_.key_rect;
    qreal full_w = std::max<qreal>(margin_width_ - 8.0, r.width());
    r.setLeft(4.0);
    r.setWidth(full_w);

    QString current = QString::fromStdString(song_.key());
    open_line_editor(r, current,
        [this](const QString& text) -> bool
        {
            // The key must begin with a Western note letter A–H so that
            // the renderer's accidental substitution and circle layout
            // work.  H is included for German notation (B natural).
            // Anything after that is accepted verbatim.
            QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
                return false;  // revert
            QChar first = trimmed.at(0).toUpper();
            if (first < QChar('A') || first > QChar('H'))
                return false;  // revert
            song_.key(trimmed.toStdString());
            rebuild();
            return true;
        });
}

void song_body_widget::edit_time_signature()
{
    const auto& ts = song_.time_sig();
    QString current = QString("%1/%2")
                        .arg(ts.count())
                        .arg(static_cast<int>(ts.kind()));

    // Stretch the editor across the full margin so "12/8" fits comfortably.
    // Anchor it vertically to the painted time-signature rect.
    QRectF r = margin_layout_.time_sig_rect;
    qreal full_w = std::max<qreal>(margin_width_ - 8.0, r.width());
    r.setLeft(4.0);
    r.setWidth(full_w);

    open_line_editor(r, current,
        [this](const QString& text) -> bool
        {
            model::time_signature new_ts;
            try
            {
                new_ts.parse_user_input(text.toStdString());
            }
            catch (const std::invalid_argument&)
            {
                // Invalid input — silently revert (caller will close the
                // editor, leaving the previous time signature unchanged).
                return false;
            }
            song_.time_sig(new_ts);
            rebuild();
            return true;
        });
}

// Note-value popup for the tempo glyph.  The seven values listed match the
// glyphs supported by margin_renderer::tempo_glyph().
void song_body_widget::edit_tempo_glyph()
{
    using time = model::chord::time;
    auto [bpm, beat_unit] = song_.tempo();
    (void)bpm;

    struct entry { const char* label; time value; };
    static const entry entries[] = {
        { "Whole",          time::WHOLE          },
        { "Half",           time::HALF           },
        { "Dotted half",    time::DOTTED_HALF    },
        { "Quarter",        time::QUARTER        },
        { "Dotted quarter", time::DOTTED_QUARTER },
        { "Eighth",         time::EIGHTH         },
        { "Dotted eighth",  time::DOTTED_EIGHTH  },
    };

    QMenu menu(this);
    for (const auto& e : entries)
    {
        QAction* a = menu.addAction(tr(e.label));
        a->setCheckable(true);
        a->setChecked(e.value == beat_unit);
        time v = e.value;
        connect(a, &QAction::triggered, this, [this, v]() {
            auto [bpm2, prev_unit] = song_.tempo();
            (void)prev_unit;
            song_.tempo({bpm2, v});
            rebuild();
        });
    }

    // Anchor the menu just below the glyph.
    QPoint anchor = mapToGlobal(QPoint(
        static_cast<int>(margin_layout_.tempo_glyph_rect.left()),
        static_cast<int>(margin_layout_.tempo_glyph_rect.bottom())));
    menu.exec(anchor);
}

void song_body_widget::edit_tempo_bpm()
{
    auto [bpm, beat_unit] = song_.tempo();

    QRectF r = margin_layout_.tempo_bpm_rect;
    // The BPM rect can be quite narrow ("= 60"); widen for typing room.
    constexpr qreal k_min_bpm_editor_w = 80.0;
    if (r.width() < k_min_bpm_editor_w)
        r.setWidth(k_min_bpm_editor_w);

    open_line_editor(r, QString::number(bpm),
        [this, beat_unit](const QString& text) -> bool
        {
            bool ok = false;
            int v = text.trimmed().toInt(&ok);
            if (!ok || v < 1 || v > 400)
            {
                // Invalid input — silently revert.
                return false;
            }
            song_.tempo({static_cast<unsigned>(v), beat_unit});
            rebuild();
            return true;
        });
}

// ---------------------------------------------------------------------------
// edit_new_bar — click handler for an insertion slot
// ---------------------------------------------------------------------------
// Opens an inline editor over the hovered ghost rectangle.  Slot choice is
// the source of truth about layout: clicking `same_line` *declares* that
// the new bar continues the last visual line, and clicking `next_line`
// *declares* that it starts a new one.  is_eol on the previous last bar
// is then written to record that declaration — never the other way
// around.  In particular, `same_line` is always offered even when the
// previous bar's is_eol is already true; the UI lets the user override
// that flag by extending the line, which simply clears is_eol on commit.
//
// Commit is atomic: either the new bar is appended *and* the previous
// bar's is_eol is updated, or neither happens.  If bar::parse_user_input
// throws on the chord text, every write performed during this commit is
// rolled back so the chart returns to exactly its pre-click state.
//
// Empty input reverts — we never want to insert a chordless bar just
// because the user clicked and pressed Enter.
//
// The slot's *kind* is captured by value (not its index), because the
// layout — and therefore the insertion_slots_ vector — is rebuilt on
// every model change.  The kind is what the user picked; the index is an
// implementation detail of the current frame.
void song_body_widget::edit_new_bar(std::size_t slot_index)
{
    if (slot_index >= insertion_slots_.size())
        return;

    const insertion_slot& slot = insertion_slots_[slot_index];
    insertion_slot_kind kind = slot.kind;

    // Widen narrow slots so there's room to type — the rendered ghost is
    // intentionally compact, but a real bar can hold several chords.
    QRectF r = slot.rect;
    constexpr qreal k_min_new_bar_editor_w = 160.0;
    if (r.width() < k_min_new_bar_editor_w)
        r.setWidth(k_min_new_bar_editor_w);

    // Clearing the hover state up front avoids a brief moment where the
    // ghost outline and the editor frame overlap during open.
    hovered_slot_ = -1;

    open_line_editor(r, QString(),
        [this, kind](const QString& text) -> bool
        {
            QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
                return false;  // revert: don't insert an empty bar

            // Stage the entire edit on a copy of the bars vector so a
            // parser exception can be cleanly aborted.  Only after the
            // new bar parses successfully do we publish the copy to the
            // model.  This keeps the operation atomic: either both the
            // previous-bar is_eol write and the append take effect, or
            // neither does.
            std::vector<model::bar> bars = song_.bars();

            // Record the UI's line-break declaration on the previous
            // last bar.  No previous bar exists for first_bar.
            if (kind != insertion_slot_kind::first_bar && !bars.empty())
                bars.back().is_eol(kind == insertion_slot_kind::next_line);

            // Parse into a fresh bar held off to the side; if it throws,
            // `bars` is untouched and we return false without publishing.
            model::bar new_bar;
            try
            {
                new_bar.parse_user_input(trimmed.toStdString());
            }
            catch (const std::exception&)
            {
                return false;  // chart returns to exact pre-click state
            }

            bars.push_back(std::move(new_bar));
            song_.bars(bars);
            rebuild();
            return true;
        },
        /*placeholder=*/tr("e.g. 1 4 5"));

    // Mark this as a bar-editing session so Tab chains.  For a new-bar
    // editor the "index" we track is *where the bar will land* after
    // commit — i.e., the current song size.  After this editor's
    // commit appends a bar at that position, the Tab-advance logic
    // computes `prev + 1`, sees that equals the new bars.size(), and
    // extends again.  Chained Tab → chained appends → rapid entry.
    editing_bar_index_ = song_.bars().size();
}

// ---------------------------------------------------------------------------
// edit_bar — click handler for an existing bar
// ---------------------------------------------------------------------------
// Opens an inline editor anchored to the clicked bar's rect, seeded with
// the bar's to_user_input() string so the user can tweak rather than
// retype.  On commit:
//   * Empty input reverts — the user can't "blank out" a bar by clearing
//     and pressing Enter.  Deletion, if added later, should be a separate
//     gesture so the meaning of a committed-empty editor stays consistent
//     with every other inline editor in this widget.
//   * Parser failure reverts atomically — we stage the parse on a copy of
//     the bar and only publish the new bars vector if parsing succeeds.
//
// The bar's *index* into song_.bars() is captured (not a pointer or rect):
// the vector may reallocate on the next edit, and the layout-derived
// pointers in bar_layout::bar become stale after every rebuild(), but
// index-based addressing stays valid as long as nothing inserts or
// removes bars while the editor is open — which the single-editor-at-a-
// time invariant guarantees.  The rect is passed in by the caller
// because it was already computed during hit-testing.
void song_body_widget::edit_bar(std::size_t bar_index, const QRectF& bar_rect)
{
    const auto& bars = song_.bars();
    if (bar_index >= bars.size())
        return;

    // Widen the editor for typing room — narrow bars (a single chord)
    // would otherwise leave the user fighting for space mid-edit.
    QRectF r = bar_rect;
    constexpr qreal k_min_bar_editor_w = 160.0;
    if (r.width() < k_min_bar_editor_w)
        r.setWidth(k_min_bar_editor_w);

    QString initial = QString::fromStdString(bars[bar_index].to_user_input());

    open_line_editor(r, initial,
        [this, bar_index](const QString& text) -> bool
        {
            QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
                return false;  // revert: keep the existing bar

            // Bounds-recheck before reading in case some other code path
            // mutated bars between editor open and commit.  Under the
            // single-editor invariant this can't happen, but the check
            // costs nothing and turns a UB into a safe no-op if the
            // invariant is ever weakened.
            if (bar_index >= song_.bars().size())
                return false;

            // Stage the parse on a local bar so a thrown parser exception
            // never reaches the model.  parse_user_input is bar-level
            // atomic on throw (chords_ is only reassigned after the new
            // chord vector is fully built), but staging on a copy makes
            // the atomicity visible at this layer too.
            model::bar staged = song_.bars()[bar_index];
            try
            {
                staged.parse_user_input(trimmed.toStdString());
            }
            catch (const std::exception&)
            {
                return false;  // chart returns to exact pre-click state
            }

            std::vector<model::bar> bars_copy = song_.bars();
            bars_copy[bar_index] = std::move(staged);
            song_.bars(bars_copy);
            rebuild();
            return true;
        });

    // Mark this as a bar-editing session AFTER opening the editor.
    // open_line_editor's teardown-of-any-stale-editor path runs
    // close_line_editor, which clears editing_bar_index_; setting it
    // here ensures the new bar editor is the one Tab will advance from.
    editing_bar_index_ = bar_index;
}

// ---------------------------------------------------------------------------
// edit_bar_by_index — re-open the bar editor on bar `bar_index`
// ---------------------------------------------------------------------------
// Used by the Tab-advance path in eventFilter after a successful commit
// has triggered rebuild().  We re-walk the freshly-rebuilt lines_ to
// find bar_index's current rect, then delegate to edit_bar.  Bails
// silently if bar_index is past the end of the song — the caller
// (eventFilter) checks for that case explicitly and routes to
// extend_with_tab() instead, so falling through here is just a
// defensive no-op.
void song_body_widget::edit_bar_by_index(std::size_t bar_index)
{
    if (bar_index >= song_.bars().size())
        return;

    // Walk lines_ in document order, the same way hit_test_bar does, to
    // find the bar_layout matching bar_index.  Layout was rebuilt by
    // the commit callback so the rects we see here are the post-commit
    // ones.
    std::size_t flat = 0;
    for (const auto& line : lines_)
    {
        for (const auto& bl : line.bars)
        {
            if (flat == bar_index)
            {
                edit_bar(bar_index, bl.rect);
                return;
            }
            ++flat;
        }
    }
    // If we fell through (lines_ doesn't yet contain bar_index — e.g.,
    // rebuild hasn't repopulated for some reason), simply do nothing.
    // No editor opens; the user is left at rest on the chart with
    // keyboard focus, which is a tolerable failure mode.
}

// ---------------------------------------------------------------------------
// extend_with_tab — Tab-extend when the just-committed bar was the last
// ---------------------------------------------------------------------------
// "Rapid input" means Tab on the last bar should keep adding bars rather
// than stopping.  The non-trivial choice is which line the new bar
// belongs to.  The rule, in order of precedence:
//
//   1. If the song is empty after commit (shouldn't happen since we
//      arrived here from a successful bar commit, but defensive), use
//      the lone first_bar slot.
//   2. If the current last bar has is_eol == true, the user
//      deliberately ended a line there — respect that and use
//      next_line, even if the visual line isn't yet full.
//   3. Otherwise, compare the last visual line's bar count to the
//      song's preferred bars_per_line.  Less than the preferred number
//      -> same_line (extend the current line).  At or over -> next_line
//      (start a new line).  Tab is explicitly NOT allowed to push the
//      visible line past bars_per_line — same_line clicks via the mouse
//      can do that and produce the continuation-dot overflow, but
//      that's a deliberate user gesture; Tab is for rapid entry of
//      conventional charts.
//
// We then delegate to edit_new_bar, which holds the slot-driven commit
// machinery (is_eol fix-up on the previous last bar, atomic stage-then-
// publish, parse rollback).  Threading the kind through edit_new_bar
// rather than reimplementing it here keeps a single source of truth for
// what "insert a new bar via slot X" means.
void song_body_widget::extend_with_tab()
{
    if (insertion_slots_.empty())
        return;  // defensive: no slots laid out (shouldn't happen here)

    // Pick the desired kind first, then find the slot of that kind.
    // The slots vector is small (1 or 2 entries) so a linear search is
    // appropriate and avoids hard-coding "slot 0 is same_line, 1 is
    // next_line" — which is true today but would be a fragile
    // assumption to bake in.
    insertion_slot_kind desired;
    if (song_.empty())
    {
        desired = insertion_slot_kind::first_bar;
    }
    else
    {
        const auto& last_line = lines_.back();
        bool last_bar_is_eol =
            !last_line.bars.empty() && last_line.bars.back().bar
            && last_line.bars.back().bar->is_eol();
        bool line_at_capacity =
            last_line.bars.size() >= song_.bars_per_line();

        desired = (last_bar_is_eol || line_at_capacity)
                ? insertion_slot_kind::next_line
                : insertion_slot_kind::same_line;
    }

    for (std::size_t i = 0; i < insertion_slots_.size(); ++i)
    {
        if (insertion_slots_[i].kind == desired)
        {
            edit_new_bar(i);
            return;
        }
    }
    // No matching slot — fall back to whatever's at index 0.  Under
    // current compute_insertion_slots logic this branch is unreachable
    // (the desired kind is always present), but keeping the fallback
    // means a future change to slot layout fails open rather than
    // dropping the Tab silently.
    edit_new_bar(0);
}

// ---------------------------------------------------------------------------
// edit_section — click handler for the section column
// ---------------------------------------------------------------------------
// Sections live exclusively on the first bar of a line (the UI never
// places one elsewhere, and the layout ignores any stray section that
// somehow ended up on a non-first bar).  So this handler always targets
// the first bar of the clicked line, regardless of whether a label is
// currently displayed.
//
// Commit semantics:
//   * Non-empty input   — assigns or renames the section on the first
//                         bar of the line.
//   * Empty input       — clears the section.  This is the only inline
//                         editor in the widget where empty commit is
//                         meaningful: bar::section("") collapses to
//                         nullopt at the model layer, so the editor
//                         just forwards the trimmed text through.
void song_body_widget::edit_section(std::size_t line_index)
{
    if (line_index >= lines_.size())
        return;

    const auto& line = lines_[line_index];
    if (line.bars.empty())
        return;

    // The first bar of this line is the target.  Map it to its index
    // in song_.bars() by summing the bar counts of all preceding lines
    // — layout order matches song-bars order, so this is just a sum.
    std::size_t target_song_index = 0;
    for (std::size_t li = 0; li < line_index; ++li)
        target_song_index += lines_[li].bars.size();

    const model::bar* target_bar = line.bars.front().bar;

    QRectF r = line.section_col_rect;

    // Widen narrow gutters so there's room to type — the painted column
    // is sized to fit a label box, but the editor needs typing room.
    constexpr qreal k_min_section_editor_w = 100.0;
    if (r.width() < k_min_section_editor_w)
        r.setWidth(k_min_section_editor_w);

    QString initial = target_bar->section()
                      ? QString::fromStdString(*target_bar->section())
                      : QString();

    // Clear any lingering hover outline so it doesn't peek out from
    // under the editor at open.
    hovered_empty_section_line_ = -1;

    open_line_editor(r, initial,
        [this, target_song_index](const QString& text) -> bool
        {
            // Note: no early-revert on empty.  The model setter treats
            // empty string as "clear the section," which is the only
            // gesture we offer for removing a section once assigned.
            QString trimmed = text.trimmed();

            if (target_song_index >= song_.bars().size())
                return false;

            // Stage on a copy and publish atomically, matching the
            // pattern used by edit_bar / edit_new_bar.  Section
            // assignment can't throw, but staging keeps the code shape
            // consistent and leaves room for future validation without
            // restructuring.
            std::vector<model::bar> bars_copy = song_.bars();
            bars_copy[target_song_index].section(trimmed.toStdString());
            song_.bars(bars_copy);
            rebuild();
            return true;
        },
        /*placeholder=*/tr("e.g. Verse"));
}

// ---------------------------------------------------------------------------
// Inline-editor plumbing
// ---------------------------------------------------------------------------
void song_body_widget::open_line_editor(const QRectF& rect,
                                        const QString& initial,
                                        std::function<bool(const QString&)> commit,
                                        const QString& placeholder)
{
    // Tear down any pre-existing editor without committing — the caller is
    // explicitly opening a fresh one.
    if (active_editor_)
        close_line_editor(/*commit_value=*/false);

    auto* edit = new QLineEdit(this);
    edit->setText(initial);
    if (!placeholder.isEmpty())
        edit->setPlaceholderText(placeholder);
    edit->selectAll();
    edit->setGeometry(rect.toRect());
    edit->setFrame(true);
    edit->show();
    edit->setFocus(Qt::MouseFocusReason);
    edit->installEventFilter(this);  // catch Esc

    active_editor_  = edit;
    editor_commit_  = std::move(commit);

    // Enter key, focus loss → commit attempt.  Re-entry from teardown is
    // harmless because close_line_editor clears active_editor_ before
    // invoking the commit callback, so a stray second call is a no-op.
    connect(edit, &QLineEdit::editingFinished, this, [this]() {
        close_line_editor(/*commit_value=*/true);
    });
}

bool song_body_widget::close_line_editor(bool commit_value)
{
    if (!active_editor_)
        return false;

    QLineEdit* edit = active_editor_;
    auto commit = std::move(editor_commit_);
    QString text = edit->text().trimmed();

    // Tear down before invoking the commit callback so the callback's
    // rebuild() can repaint without the (about-to-be-deleted) editor on
    // top, and so any focus-loss noise during teardown can't re-enter us.
    active_editor_ = nullptr;
    // Drop the bar-edit marker too — Tab-advance only makes sense while
    // a bar editor is open, and we're about to either commit it, revert
    // it, or both.  Any follow-up open_line_editor (e.g. from the Tab-
    // advance path itself) will re-set this if appropriate.
    editing_bar_index_.reset();
    edit->removeEventFilter(this);
    edit->hide();
    edit->deleteLater();

    bool committed = false;
    if (commit_value && commit)
    {
        // The callback may return false to indicate validation failure;
        // in that case we simply drop the change and leave the previous
        // value intact — the editor is already closed.  We repaint
        // explicitly so any region the editor occupied gets restored
        // cleanly (e.g. the gray "Title" placeholder reappears in full
        // after an empty-title revert on a fresh song).
        committed = commit(text);
        if (!committed)
            update();
    }
    else
    {
        // Esc/cancel path — no commit, but we still need a clean repaint.
        update();
    }
    return committed;
}

bool song_body_widget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == active_editor_ && event->type() == QEvent::KeyPress)
    {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape)
        {
            close_line_editor(/*commit_value=*/false);
            return true;
        }

        // Tab: rapid-entry shortcut for bar editing.  We only want this
        // when the active editor *is* a bar editor — Tab in the title /
        // tempo / section editors should fall through to QLineEdit's
        // default focus-traversal behavior.  editing_bar_index_ is the
        // discriminator (set by edit_bar AND edit_new_bar; reset by
        // close_line_editor).
        //
        // Shift+Tab on most platforms arrives as Qt::Key_Backtab rather
        // than Key_Tab with the Shift modifier — we intentionally do
        // NOT consume Backtab here so it keeps its default behavior
        // (no "previous bar" gesture was requested).
        if (ke->key() == Qt::Key_Tab && editing_bar_index_.has_value())
        {
            // Snapshot the current bar index before commit — the
            // close_line_editor → commit callback chain will clear
            // editing_bar_index_ as part of teardown.  For an existing
            // bar editor this is the bar's index; for a new-bar editor
            // it's the index the new bar will land at after commit
            // (== song_.bars().size() at editor open time, which is
            // also == the new bar's final index after commit appends).
            std::size_t prev = *editing_bar_index_;

            // Try to commit.  If commit fails (empty input, parser
            // throw), do NOT advance — the user's edit was rejected and
            // moving the editor onto a new bar would silently lose
            // their attempt.  They're left back at the chart, free to
            // re-click and try again.
            bool committed = close_line_editor(/*commit_value=*/true);
            if (committed)
            {
                // After a successful commit + rebuild, decide whether
                // the "next bar" already exists or needs to be
                // appended.  An existing-bar commit doesn't change
                // bars.size(), so prev + 1 < size when there's a
                // following bar.  A new-bar commit appends one, so
                // bars.size() is now prev + 1 — same condition still
                // distinguishes correctly between "edit existing next"
                // and "extend the song again."
                if (prev + 1 < song_.bars().size())
                    edit_bar_by_index(prev + 1);
                else
                    extend_with_tab();
            }

            // Consume the Tab event regardless of whether we advanced
            // (Qt would otherwise try to traverse focus out of the
            // already-destroyed QLineEdit, which is a no-op but logs
            // a warning in some builds).
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
    paint_title(painter, width(), /*stash_hit_rect=*/false);
    paint_margin(painter,
                 QRectF(0, title_height(), margin_width_, height() - title_height()),
                 /*stash_hit_rects=*/false);
    paint_divider(painter);
    painter.setPen(QPen(Qt::black, 1.0));
    {
        // Printouts: never include the selection background, even if the
        // user had bars selected when triggering print.
        std::size_t first_bar_index = 0;
        for (const auto& line : lines_)
        {
            paint_line(painter, line, first_bar_index,
                       /*show_selection=*/false);
            first_bar_index += line.bars.size();
        }
    }

    painter.restore();
}

} // namespace nashville::view
