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

    qreal plain_h_bare   = plain_bar_height(false);
    qreal plain_h_art    = plain_bar_height(true);
    qreal duration_h_bare = duration_bar_height(false);
    qreal duration_h_art  = duration_bar_height(true);

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
        line.is_duration_mode = (actual_bar_h == duration_h_bare || actual_bar_h == duration_h_art);
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
    const bool title_empty = title.isEmpty();

    // Empty titles render as gray placeholder text.
    QString display_title = title_empty
                            ? QString::fromUtf8(k_title_placeholder)
                            : title;

    qreal text_w  = fm.horizontalAdvance(display_title);
    qreal x       = (widget_width - text_w) / 2.0;
    qreal baseline = k_title_padding + fm.ascent();

    painter.setPen(QPen(title_empty ? k_placeholder_color : Qt::black, 1.0));
    painter.drawText(QPointF(x, baseline), display_title);

    // Underline directly beneath the text — only for real titles.  An
    // underlined placeholder reads as a real title.
    if (!title_empty)
    {
        qreal underline_y = std::round(baseline + fm.descent() + 1.0);
        painter.drawLine(QPointF(x, underline_y), QPointF(x + text_w, underline_y));
    }

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
    for (const auto& line : lines_)
        paint_line(painter, line);
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

    // If an inline editor is open and the click lands outside it, commit
    // the current editor before hit-testing the new click.  This lets the
    // user hop straight from one editable element to another.  Clicks
    // inside the editor itself fall through normally so the QLineEdit
    // handles them.
    if (active_editor_ && !active_editor_->geometry().contains(event->pos()))
        close_line_editor(/*commit_value=*/true);

    // Divider drag wins over edit hit-testing.
    if (near_divider(event->pos().x()))
    {
        dragging_divider_ = true;
        drag_start_x_      = event->pos().x();
        drag_start_margin_ = margin_width_;
        setCursor(Qt::SplitHCursor);
        return;
    }

    const QPointF p = event->pos();
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

    if (near_divider(event->pos().x()))
    {
        setCursor(Qt::SplitHCursor);
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
        return;
    }

    setCursor(Qt::ArrowCursor);
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

void song_body_widget::close_line_editor(bool commit_value)
{
    if (!active_editor_)
        return;

    QLineEdit* edit = active_editor_;
    auto commit = std::move(editor_commit_);
    QString text = edit->text().trimmed();

    // Tear down before invoking the commit callback so the callback's
    // rebuild() can repaint without the (about-to-be-deleted) editor on
    // top, and so any focus-loss noise during teardown can't re-enter us.
    active_editor_ = nullptr;
    edit->removeEventFilter(this);
    edit->hide();
    edit->deleteLater();

    if (commit_value && commit)
    {
        // The callback may return false to indicate validation failure;
        // in that case we simply drop the change and leave the previous
        // value intact — the editor is already closed.  We repaint
        // explicitly so any region the editor occupied gets restored
        // cleanly (e.g. the gray "Title" placeholder reappears in full
        // after an empty-title revert on a fresh song).
        bool committed = commit(text);
        if (!committed)
            update();
    }
    else
    {
        // Esc/cancel path — no commit, but we still need a clean repaint.
        update();
    }
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
    for (const auto& line : lines_)
        paint_line(painter, line);

    painter.restore();
}

} // namespace nashville::view
