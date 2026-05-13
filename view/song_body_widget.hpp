#pragma once

#include "../model/song.hpp"
#include "chord_renderer.hpp"
#include "bar_renderer.hpp"
#include "margin_renderer.hpp"
#include "layout_structs.hpp"
#include <QWidget>
#include <vector>
#include <functional>
#include <set>
#include <string>
#include <optional>

class QLineEdit;

namespace nashville::view
{

// Renders a song chart and supports click-to-edit on the title and on the
// three margin elements (key, time signature, tempo), plus click-to-insert
// for new bars via dashed "ghost" rectangles that appear on hover.  The
// widget holds a non-const reference to the song because the chart is
// always editable — there is no read-only display mode.
//
// Editing UX (all inline, no modal dialogs):
//   * Title          — QLineEdit overlay
//   * Key            — QLineEdit overlay
//   * Time signature — QLineEdit overlay (parsed via
//                      time_signature::parse_user_input; invalid input
//                      reverts on commit)
//   * Tempo glyph    — popup menu of note values
//   * Tempo BPM      — QLineEdit overlay (numeric, 1–400; invalid reverts)
//   * New bar        — hover shows a dashed ghost rectangle; click opens a
//                      QLineEdit whose text is fed to bar::parse_user_input.
//                      At most two slots are offered (after the last bar
//                      and at the start of a new line), reducing to one
//                      first-bar slot when the song is empty.
//   * Existing bar   — single click selects the bar (light-gray fill);
//                      Ctrl+click toggles inclusion in the selection;
//                      Shift+click extends from the selection anchor.
//                      Double-click clears the selection and opens an
//                      inline QLineEdit seeded with the bar's current
//                      to_user_input() string; commit replaces the bar
//                      via parse_user_input.  Empty input or parser
//                      failure reverts.  Selection enables clipboard
//                      operations (Ctrl+C / Ctrl+X / Ctrl+V); Esc or a
//                      click on empty chart space clears the selection.
//                      In bar edit mode, Enter commits in place and
//                      Tab commits then advances the editor onto the
//                      next bar in song order — appending a new bar
//                      if there is no next, with the new bar going on
//                      the same visual line when there's room (per
//                      bars_per_line) or on a fresh line otherwise.
//                      User-set is_eol on the current bar is respected
//                      (Tab will not erase a manual line break).
//   * Section column — click in the left gutter opens a QLineEdit
//                      seeded with the line's section label (or empty
//                      if none); commit assigns or replaces the
//                      section on the first bar of the line.  Empty
//                      commit clears the section — the one place in
//                      this widget where empty input is not "revert"
//                      but a meaningful action.  The gutter is always
//                      reserved with a small minimum width so the
//                      first section can be bootstrapped on a
//                      label-less song.
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
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;   // clear hover outlines
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
    void paint_line(QPainter& painter, const line_layout& line,
                    std::size_t first_bar_index,
                    bool show_selection = true) const;
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

    // Click handler for an insertion slot.  Opens an inline editor over the
    // slot's rect; on commit, appends a new bar to the model (fixing up the
    // previous bar's is_eol flag if needed for "same line" vs "new line")
    // and feeds the text to bar::parse_user_input().  Empty input reverts.
    void edit_new_bar(std::size_t slot_index);

    // Click handler for an existing bar.  Opens an inline editor seeded
    // with bar::to_user_input() and, on commit, replaces that bar via
    // bar::parse_user_input().  Empty input or parser failure reverts.
    void edit_bar(std::size_t bar_index, const QRectF& bar_rect);

    // Click handler for the section column.  Sections live exclusively
    // on the first bar of a line, so this always targets that bar.  The
    // editor is seeded with the line's existing section label (or empty
    // if none); commit assigns, renames, or — if empty — clears the
    // section.
    void edit_section(std::size_t line_index);

    // --- Insertion slots ---
    // Computed at the end of compute_layout from the current model state.
    // Painted only when hovered, so the chart stays uncluttered.
    void compute_insertion_slots(const QRectF& content_rect,
                                 qreal bars_left,
                                 qreal last_line_bottom,
                                 qreal last_line_bar_h);
    void paint_insertion_slot(QPainter& painter, const insertion_slot& slot) const;
    int  hit_test_insertion_slot(const QPointF& p) const;  // returns index, or -1

    // Returns the bar_index (into song_.bars()) under `p`, or -1 if none.
    // Walks `lines_` looking for a bar_layout whose rect contains the
    // point, then maps that back to an index in song_.bars() by counting
    // bars in order — line breaks don't insert any "blank" slot in
    // song_.bars(), so this is just an accumulator.  If `out_rect` is
    // non-null and a bar is found, the matching rect is written through
    // so the caller can position an editor without re-walking the
    // layout.
    int  hit_test_bar(const QPointF& p, QRectF* out_rect = nullptr) const;

    // Returns the line index whose section_col_rect contains `p`, or -1
    // if none.  The section column is always reserved with a minimum
    // width so this hit-test works even for label-less songs.
    int  hit_test_section_col(const QPointF& p) const;

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
    // Returns true iff the commit callback was both invoked AND returned
    // true.  Esc/cancel and validation-rejected commits both return
    // false.  The return is consulted by the Tab-advance path so a
    // rejected commit doesn't silently move the editor onto a different
    // bar — the user would lose their input without realising it.
    bool close_line_editor(bool commit_value);

    QLineEdit* active_editor_      = nullptr;
    std::function<bool(const QString&)> editor_commit_;

    // Set by edit_bar before opening the editor, cleared by
    // close_line_editor.  When set, Tab in the editor (intercepted in
    // eventFilter) commits and then opens an editor on bar index+1 if
    // one exists.  Other inline editors (title, key, time-sig, tempo,
    // new-bar, section) leave this unset so Tab falls back to its
    // normal QLineEdit focus-traversal behavior there.
    std::optional<std::size_t> editing_bar_index_;

    // Helper for the Tab-advance path: looks up the bar at `bar_index`
    // in the just-rebuilt layout and, if found, opens the bar editor on
    // it.  Called from eventFilter after a successful Tab-commit.
    void edit_bar_by_index(std::size_t bar_index);

    // Helper for the Tab-advance path when the just-committed bar was
    // the song's last: opens a new-bar editor on the appropriate
    // insertion slot, picking same_line vs. next_line so that the next
    // bar respects the song's preferred bars_per_line (and any
    // user-set is_eol on the current last bar).  Reuses edit_new_bar's
    // commit machinery — only the choice of slot is the Tab-specific
    // bit.
    void extend_with_tab();

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

    // --- Insertion slots (populated during compute_layout) ---
    // 0–2 entries.  Index of currently-hovered slot, or -1 if none.
    std::vector<insertion_slot> insertion_slots_;
    int                         hovered_slot_ = -1;

    // Line index whose section column is being hovered AND has no section
    // label yet, or -1.  Drives a dashed outline on the empty gutter to
    // make the click target visible.  Lines that already carry a label
    // are not tracked here — their painted box is its own affordance.
    int                         hovered_empty_section_line_ = -1;

    // --- Selection ---
    // Selected bars by flat index into song_.bars().  A click on a bar
    // (without modifiers) replaces the selection; Ctrl+click toggles a
    // single bar; Shift+click extends from selection_anchor_ to the
    // clicked bar.  Esc clears the selection; so does any non-bar click
    // on chart whitespace.  Selected bars are painted with a light-gray
    // fill behind the bar contents.
    //
    // selection_anchor_ is the most recently single- or Ctrl-clicked bar;
    // it's the pivot for Shift+click range extension.  Range selections
    // do not move the anchor, matching the file-manager idiom (the
    // anchor only changes when the user "starts a new selection").
    std::set<std::size_t>      selected_bars_;
    std::optional<std::size_t> selection_anchor_;

    // Clears selected_bars_/selection_anchor_ and repaints if anything
    // was selected.  Returns true if the selection actually changed.
    bool clear_selection();

    // Selection-mutating click helpers.  Each repaints as needed.
    void select_bar_only(std::size_t bar_index);
    void toggle_bar_in_selection(std::size_t bar_index);
    void extend_selection_to(std::size_t bar_index);

    // --- Clipboard ---
    // Bars are serialised via bar::to_user_input() (the same round-
    // trippable text form used by edit_bar) so a paste is just a
    // parse_user_input() into a fresh bar.  Storing as strings — not
    // as live model::bar objects — keeps the clipboard immune to model
    // changes (renames, reparses) between copy and paste.
    std::vector<std::string> clipboard_;

    void copy_selection();      // populates clipboard_ from selection
    void cut_selection();       // copy + delete selected bars
    void delete_selection();    // remove selected bars from the song
    void paste_clipboard();     // insert clipboard after last selected bar
                                // (or at song end if no selection)

    void init_fonts();
};

} // namespace nashville::view
