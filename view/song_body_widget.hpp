#pragma once

#include "../model/song.hpp"
#include "chord_renderer.hpp"
#include "bar_renderer.hpp"
#include "margin_renderer.hpp"
#include "layout_structs.hpp"
#include <QWidget>
#include <vector>
#include <functional>

class QLineEdit;

namespace nashville::view
{

// Renders a song chart and supports click-to-edit on the title and on the
// three margin elements (key, time signature, tempo).  The widget holds a
// non-const reference to the song because the chart is always editable —
// there is no read-only display mode.
//
// Editing UX (all inline, no modal dialogs):
//   * Title          — QLineEdit overlay
//   * Key            — QLineEdit overlay
//   * Time signature — QLineEdit overlay (parsed via
//                      time_signature::parse_user_input; invalid input
//                      reverts on commit)
//   * Tempo glyph    — popup menu of note values
//   * Tempo BPM      — QLineEdit overlay (numeric, 1–400; invalid reverts)
//
// On commit (Enter or focus loss), validators that reject input cause the
// edit to be silently discarded and the previous value retained.  Esc
// always cancels without committing.
class song_body_widget : public QWidget
{
    Q_OBJECT

public:
    explicit song_body_widget(model::song& song, QWidget* parent = nullptr);

    // Call after font changes or song data changes.
    void rebuild();

    // For printing: same layout/paint logic targeting an arbitrary rect.
    void paint_to_rect(QPainter& painter, const QRectF& page_rect) const;

    int margin_width() const { return margin_width_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // --- Layout ---
    void compute_layout(const QRectF& content_rect);
    qreal plain_bar_height(bool has_articulation = false) const;
    qreal duration_bar_height(bool has_articulation = false) const;
    qreal title_height() const;

    // --- Painting ---
    // stash_hit_rects controls whether to update the hit-test rects
    // (title_rect_ and margin_layout_) for click handling.  Must be false
    // when painting to a transformed coordinate space (printing).
    void paint_title(QPainter& painter, qreal widget_width,
                     bool stash_hit_rect = true) const;
    void paint_margin(QPainter& painter, const QRectF& margin_rect,
                      bool stash_hit_rects = true) const;
    void paint_divider(QPainter& painter) const;
    void paint_line(QPainter& painter, const line_layout& line) const;
    void paint_section_label(QPainter& painter,
                           const QString& label,
                           const QRectF& col_rect) const;
    void paint_continuation_dot(QPainter& painter,
                               const QRectF& preceding_bar_rect,
                               qreal num_center_y) const;

    // --- Edit handlers ---
    // All four field editors open inline overlays.
    void edit_title();
    void edit_key();
    void edit_time_signature();
    void edit_tempo_glyph();   // popup menu of note values
    void edit_tempo_bpm();     // inline numeric editor

    // --- Inline-editor plumbing ---
    // Open a line-edit overlay covering `rect`, prefilled with `initial`,
    // selected and focused.  `commit` is invoked with the trimmed text on
    // Enter or focus loss; if it returns false the change is silently
    // discarded.  Esc cancels (commit not invoked at all).
    // `placeholder`, if non-empty, is shown in gray when the editor is
    // empty (same QLineEdit placeholder semantics — disappears on type).
    void open_line_editor(const QRectF& rect,
                          const QString& initial,
                          std::function<bool(const QString&)> commit,
                          const QString& placeholder = QString());
    void close_line_editor(bool commit_value);

    QLineEdit* active_editor_      = nullptr;
    std::function<bool(const QString&)> editor_commit_;

    // --- Draggable divider ---
    bool near_divider(int x) const;
    bool dragging_divider_ = false;
    int  drag_start_x_      = 0;
    int  drag_start_margin_ = 0;

    // --- Constants ---
    static constexpr qreal k_title_padding       = 16.0;  // above and below title text
    static constexpr qreal k_line_spacing        = 16.0;  // spacing after a section-end rule
    static constexpr qreal k_line_spacing_normal = 10.0;  // uniform spacing between all other lines
    static constexpr qreal k_inter_bar_spacing   = 6.0;
    static constexpr qreal k_content_padding    = 12.0;
    static constexpr int   k_divider_hit_width  = 5;
    static constexpr int   k_min_margin_width   = 60;
    static constexpr int   k_max_margin_width   = 200;
    static constexpr int   k_default_margin_width = 100;

    // Placeholder text shown when title is empty (in both painted form
    // and the inline editor's QLineEdit).
    static constexpr const char* k_title_placeholder = "Title";

    // --- Data ---
    model::song&             song_;
    std::vector<line_layout> lines_;
    chord_renderer::Fonts    fonts_;
    int                      margin_width_ = k_default_margin_width;

    // --- Hit-test rects (populated during paintEvent, in widget coords) ---
    mutable QRectF        title_rect_;
    mutable margin_layout margin_layout_;

    void init_fonts();
};

} // namespace nashville::view
