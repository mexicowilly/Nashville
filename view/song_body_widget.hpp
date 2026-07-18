#pragma once

#include "../model/song.hpp"
#include "chord_renderer.hpp"
#include "bar_renderer.hpp"
#include "margin_renderer.hpp"
#include "layout_structs.hpp"
#include "annotation_layer.hpp"
#include "../page_geometry.hpp"
#include <QWidget>
#include <vector>
#include <functional>
#include <set>
#include <string>
#include <optional>
#include <cstdint>

class QLineEdit;
class QContextMenuEvent;

namespace nashville::view
{

// Forward declaration so song_body_widget can hold a pointer to the overlay
// without a full include (which would pull QPrinter headers and cause
// namespace pollution when song_body_widget.hpp is included by main_window.cpp).
class modal_overlay;

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

    // Supply the application-level modal overlay so this widget can show
    // Wayland-safe prompts.  Must be called before any prompt_*_for_selection
    // methods are invoked.  The pointer is non-owning; the overlay's lifetime
    // is managed by main_window.
    void set_overlay(modal_overlay* overlay) { overlay_ = overlay; }

    // Call after font changes or song data changes.
    void rebuild();

    // Apply a new chart font scale (clamped to the ui_settings range) and
    // relayout.  The scale multiplies every font on the chart and resizes
    // the left-margin column proportionally so the whole chart grows as a
    // unit.  No-op if the scale is unchanged.  Called by app when the user
    // picks a size from the View > Text size menu; newly opened tabs pick
    // up the persisted scale on construction instead, so this only needs to
    // touch already-open charts.
    void apply_font_scale(qreal scale);

    // Apply a new title or margin font scale (clamped to the ui_settings
    // header range) and relayout.  These size the song title and the left
    // margin gutter (key / time / tempo) independently of the chart body's
    // font_scale — the two elements that sit outside the bar grid's
    // automatic per-line fit.  No-op if unchanged.  Newly opened tabs read
    // the persisted values on construction, so these only touch open charts.
    // A title-scale change re-paginates (the title band's height feeds the
    // page break math); a margin-scale change only affects painting, but
    // both route through rebuild() for simplicity.
    void apply_title_scale(qreal scale);
    void apply_margin_scale(qreal scale);

    // Apply a new page size/margins (from ui_settings — page size and
    // margins are app-wide, not per-song) and relayout.  Called by
    // main_window when the user changes the Page Setup dialog; newly
    // opened tabs pick up the persisted geometry on construction instead,
    // so this only needs to touch already-open charts.  No-op if the
    // geometry is unchanged.
    void apply_page_geometry(const page_geometry& geo);

    // The page geometry currently in effect (content width/height, margins,
    // paper size) and how many pages the current chart spans.  Read by the
    // print path to size/emit each physical page.
    const page_geometry& current_page_geometry() const { return page_geo_; }
    int page_count() const { return page_count_; }

    // For printing: paint the given 0-based page at (approximately) 1:1
    // scale into target_rect (a page rect in the destination painter's own
    // coordinate space — e.g. a QPrinter's pageRect).  Only that page's
    // title (page 0 only) / margin gutter (page 0 only) / lines are
    // painted, so callers loop pages and call printer->newPage() between
    // calls.  Scale is target_rect.width() / page_geo_.size.width(), which
    // is 1.0 when the destination page size matches page_geo_ exactly.
    void paint_page_to_rect(QPainter& painter, int page_index,
                            const QRectF& target_rect) const;

    int margin_width() const { return margin_width_; }

    // --- Selection-driven bar attribute edits ---
    // These operate on the currently-selected bars (selected_bars_) and
    // are no-ops when nothing is selected.  Exposed so the main-window
    // "Bar" menu in app.cpp can act on the same selection the user sees
    // highlighted in the chart, without app.cpp poking at our internals.
    // Both rebuild() on success.
    bool has_selection() const { return !selected_bars_.empty(); }
    void apply_repeat_to_selection(model::bar::repeat_status st);
    // `voltas` is the set of 0-indexed volta numbers to assign to every
    // selected bar (replacing any existing voltas on those bars).  Pass
    // an empty set to clear.  The "1-indexed in the UI" convention is
    // handled by callers (app.cpp, the right-click handler) — by the
    // time we reach this method we're already in storage coordinates.
    void apply_voltas_to_selection(const std::set<unsigned>& voltas);

    // Opens a modal QInputDialog prompting for a custom number of beats for
    // the selected bars.  Empty input (or the song's default beat count)
    // clears the override, restoring the bars to normal.  Out-of-range or
    // non-numeric input is rejected silently.  Public because both the
    // right-click context menu and the menubar "Bar > Custom beats..." action
    // invoke it.
    void prompt_beats_for_selection();

    // Sets (or clears, when beats == nullopt) the number_of_beats override on
    // every selected bar and rebuilds.
    void apply_beats_to_selection(std::optional<unsigned> beats);

    // Returns the shared number_of_beats across all selected bars iff they
    // all agree (including all nullopt), nullopt otherwise.
    std::optional<std::optional<unsigned>> common_beats_of_selection() const;

    // Opens a modal QInputDialog prompting for a comma-separated list of
    // 1-indexed volta numbers, prefilled from the current selection iff
    // every selected bar carries the same volta set.  On accept, parses
    // the text (whitespace tolerant, blanks ignored) and applies the
    // result to every selected bar via apply_voltas_to_selection.
    // Invalid tokens (non-numeric, zero, or out-of-range) silently abort
    // the apply — the dialog already gave the user a chance to fix
    // typos and re-confirming would be noisy.  An empty string is a
    // valid input meaning "clear all voltas on the selection."  Public
    // because both the right-click context menu and the menubar's
    // "Bar > Voltas..." action invoke it.
    void prompt_voltas_for_selection();

    // Returns the shared repeat bitmask across all selected bars iff they
    // all agree, nullopt otherwise (mixed selection or empty selection).
    // The value is an int (ANDed repeat_status bits), not the enum itself,
    // because a bar may carry both BEGIN and END simultaneously.
    std::optional<int> common_repeat_of_selection() const;
    // Same idea for the volta set.  When all selected bars carry the
    // same volta set (including all empty), returns that set; otherwise
    // nullopt.  The "Voltas..." dialog uses this to prefill its text
    // input only when there's an unambiguous starting value to show.
    std::optional<std::set<unsigned>> common_voltas_of_selection() const;

    // Opens a modal prompt for the modulation (new key) that the selected
    // bar(s) begin.  Prefilled from the selection iff every selected bar
    // carries the same modulation string.  An empty string clears the
    // modulation on the selection — symmetric with the section / voltas /
    // beats editors.  The text is stored verbatim in the model (e.g. "Bb",
    // "F#m"); the renderer substitutes the proper accidental glyphs for
    // display.  Public because both the right-click context menu and the
    // menubar "Bar > Modulation..." action invoke it.
    void prompt_modulation_for_selection();

    // Sets (or clears, when mod is empty) the modulation string on every
    // selected bar and rebuilds.
    void apply_modulation_to_selection(const std::string& mod);

    // Returns the shared modulation string across all selected bars iff
    // they all agree (including all empty), nullopt otherwise.  The
    // "Modulation..." dialog uses this to prefill only when unambiguous.
    std::optional<std::string> common_modulation_of_selection() const;

    // Opens a modal prompt for the song's preferred bars-per-line, then
    // reflows the chart to honour it.  Song-level (no bar selection
    // required).  Public because the menubar "Song > Bars per line..."
    // action invokes it.
    void prompt_bars_per_line();

    // Sets the song's bars_per_line preference to n and reflows the
    // existing layout while PRESERVING the user's line breaks: existing
    // is_eol flags are never removed or moved and lines are never merged.
    // Only a line longer than n bars is split (a break inserted every n
    // bars within it).  Raising bars-per-line thus leaves the current
    // layout untouched; lowering it splits the over-long lines.  Section
    // labels (which ride on each line's first bar) are never orphaned.
    // n == 0 is ignored.
    void apply_bars_per_line(unsigned n);

    // Inserts a new bar adjacent to the selection: at the position of
    // the lowest selected index when `after` is false ("Insert 1
    // before"), or one past the highest selected index when `after` is
    // true ("Insert 1 after").  Opens an inline editor anchored to the
    // selected-bar rect that defines the anchor; the bar is only
    // actually appended to the model if the user commits non-empty,
    // parseable input — matching edit_new_bar's "no empty bars in the
    // model" rule.  Honours the song's bars_per_line preference: if
    // the line that receives the new bar wasn't already extended past
    // bars_per_line and the insertion would push it over, an is_eol is
    // planted at the bars_per_line'th bar of that line, pushing the
    // rest onto a new line.  Lines already extended past bars_per_line
    // (authored deliberately) keep extending — the insert grows the
    // extension by one rather than splitting it.  No-op when the
    // selection is empty.
    void insert_bar_relative_to_selection(bool after);

    // Sets is_eol = true on every selected bar.  Idempotent — bars
    // that already end a line are unchanged.  No-op when the selection
    // is empty.  Public for the same reason as the other selection-
    // driven actions: both the right-click context menu and the
    // menubar's "Bar > End line" action invoke it.
    void apply_end_line_to_selection();

    // Removes every selected bar from the song.  No-op when the
    // selection is empty.  Public so the menubar's "Bar > Delete"
    // action (and its Del/Backspace shortcut) and the right-click
    // context menu can both invoke it on the same selection the user
    // sees highlighted.  The internal cut_selection() path also routes
    // through this — there's one delete implementation.
    // The menubar's Delete action (with Del/Backspace shortcuts) and
    // the right-click context menu both call this.  It dispatches by
    // which selection is live: if an annotation (text box or
    // connector) is selected, delete that; otherwise fall through to
    // delete_selection() which handles bars.  Routing both selection
    // domains through one function means the QAction shortcut can't
    // race the widget's own keyPressEvent — there is only one path.
    void apply_delete_to_selection()
    {
        if (annotation_layer_.key_press(Qt::Key_Delete, Qt::NoModifier))
        {
            update();
            return;
        }
        delete_selection();
    }

    // --- Cut / copy / paste ---
    // Public so the menubar's "Bar > Cut/Copy/Paste" actions (bound to
    // the standard Ctrl+X/C/V shortcuts in app.cpp) and the right-click
    // context menu can both drive the same clipboard_ — one
    // implementation, multiple entry points, matching the
    // apply_delete_to_selection pattern above. No-op when there's no
    // selection (cut/copy) or an empty clipboard (paste).
    void apply_copy_to_selection()  { copy_selection(); }
    void apply_cut_to_selection()   { cut_selection(); }
    // Inserts the clipboard's bars immediately after the
    // highest-indexed selected bar (or at the song's end if nothing
    // is selected). Doesn't extend that line past the song's
    // preferred bars_per_line — overflow flows onto the following
    // line(s) instead, same as "Insert 1 after" — see
    // paste_clipboard() for the full rule.
    void apply_paste_after_selection() { paste_clipboard(); }

    // --- Undo / redo ---
    // Covers bar content and structure (chords, insert/delete, cut/
    // paste, drag-reorder, repeat/voltas/custom-beats/modulation/
    // end-line, section labels) and the song header fields edited in
    // this widget (title, key, tempo, time signature, bars-per-line,
    // margin width). Annotation edits (text boxes/connectors) are a
    // separate, not-yet-covered history — see push_undo_snapshot()'s
    // comment for why. Public so the menubar's "Edit > Undo/Redo"
    // actions (and their Ctrl+Z/Shift+Ctrl+Z shortcuts) can drive it.
    void apply_undo();
    void apply_redo();
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }

    // Whether the clipboard has anything to paste. Used to gate the
    // enabled state of the Paste menu/context-menu items.
    bool has_clipboard() const { return !clipboard_.empty(); }

    // --- Annotation tool mode ---
    // Switches the widget between bar-editing mode (the default) and one
    // of the annotation tools.  When an annotation tool is active, clicks
    // on the chart create or manipulate text boxes / connectors instead
    // of selecting bars.  Esc returns to bar mode — that's handled
    // inside annotation_layer::key_press, which also runs before our
    // own Esc handler (see keyPressEvent).
    void set_annotation_tool(annotation_layer::tool t)
    {
        annotation_layer_.set_tool(t);
        // Cursor needs a refresh; mouseMoveEvent will set it again on
        // next mouse move, but unsetCursor() here keeps a stale cursor
        // (e.g. crosshair from the previous tool) from lingering until
        // the user moves.  update() repaints any selection chrome that
        // was cleared by set_tool().
        unsetCursor();
        update();
    }
    annotation_layer::tool current_annotation_tool() const
    {
        return annotation_layer_.current_tool();
    }

    // Read-only access to the annotation layer.  The Insert-menu wiring
    // in app.cpp uses this to gate its enabled state on the layer's
    // selection.
    const annotation_layer& annotations_view() const
    {
        return annotation_layer_;
    }

signals:
    // Emitted by edit_title() when the user commits a title that differs
    // from the current one.  The song's name has already been updated in
    // the model by the time this fires.  The host (song_tab) uses it to
    // push the rename to the database immediately and to refresh the tab
    // label / song list, which the content-blind debounced save path can't
    // do on its own (it has no idea a name changed).
    void title_changed();

    // Emitted after the user confirms a deletion that removes the last
    // remaining bar(s), leaving the song empty.  The model is already empty
    // by the time this fires.  song_tab listens for it to authorise the one
    // autosave allowed to persist an empty bar set — the autosave otherwise
    // refuses to overwrite a non-empty stored song with an empty one, as a
    // guard against a stray save wiping the chart.
    void song_emptied();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;   // clear hover outlines
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // --- Layout ---
    void compute_layout(const QRectF& content_rect);
    // Second phase of compute_layout: breaks the contiguously-laid-out
    // lines_ across pages (page_geometry.hpp), assigning each line a
    // page_index and translating its geometry into whole-canvas
    // coordinates.  Sets page_count_.
    void paginate_lines(const QRectF& content_rect);
    // Vertically translate every geometry field a line owns (its rect,
    // section column, and bar rects) by dy — used by paginate_lines to
    // move a whole line onto its assigned page.
    static void translate_line(line_layout& line, qreal dy);
    qreal plain_bar_height(bool has_articulation = false) const;
    qreal duration_bar_height(bool has_articulation = false) const;
    qreal title_height() const;

    // The x of the left edge of the section-label gutter: the page's left
    // margin.  The gutter runs from here for margin_width_ points, then the
    // bars begin.  The draggable divider sits at page_left() + margin_width_.
    // Centralising this keeps rebuild(), paint_margin, paint_divider, and
    // the divider hit-test/drag math agreeing on where the gutter lives
    // once a nonzero page margin insets it from x=0.
    qreal page_left() const { return page_geo_.margins.left; }
    // The x of the draggable margin divider (gutter's right edge).
    qreal divider_x() const { return page_left() + margin_width_; }

    // --- Painting ---
    // stash_hit_rects controls whether to update the hit-test rects
    // (title_rect_ and margin_layout_) for click handling.  Must be false
    // when painting to a transformed coordinate space (printing).
    // top_offset shifts the title's baseline/hit-rect down by the page's
    // top margin so the title sits inside the printable area.
    void paint_title(QPainter& painter, qreal widget_width,
                     bool stash_hit_rect = true, qreal top_offset = 0.0) const;
    // Draws the repeated running header — the song title plus " pg. N" — at
    // the top of a continuation page (page_index >= 1).  Page 0 shows the
    // full title via paint_title instead and never gets a page number.
    // Uses the title font/scale so the running head reads as a smaller
    // echo of the title.  Never stashes a hit rect: only the page-0 title
    // is click-to-edit.
    void paint_running_header(QPainter& painter, int page_index) const;
    // Height reserved for a header band (title/running-head text plus its
    // padding and the gap down to the first line).  Equals the page-0 title
    // band, so continuation-page headers get the same breathing room.  This
    // is what paginate_lines reserves at the top of pages >= 1.
    qreal page_header_band() const;
    void paint_margin(QPainter& painter, const QRectF& margin_rect,
                      bool stash_hit_rects = true) const;
    void paint_divider(QPainter& painter) const;
    // Draws the "desk" behind the sheets, each page as a white sheet with a
    // soft drop shadow and border, and the dashed margin guides on every
    // page.  Called first in paintEvent so all chart content paints on top.
    void paint_pages_backdrop(QPainter& painter) const;
    void paint_line(QPainter& painter, const line_layout& line,
                    std::size_t first_bar_index,
                    bool show_selection = true) const;
    // Resolves the time signature actually in effect at song_.bars()[bar_index]:
    // that bar's own override if it has one, else the nearest preceding
    // bar's override, else the song's default. A bar-level time_sig()
    // persists forward until the next bar that sets its own, the same way
    // a time signature change on a printed staff holds until the next one —
    // bar_renderer only ever sees one bar at a time, so this chain has to
    // be resolved by whoever walks the bars in order.
    model::time_signature effective_time_signature(std::size_t bar_index) const;
    void paint_section_label(QPainter& painter,
                           const QString& label,
                           const QRectF& col_rect) const;
    void paint_continuation_dot(QPainter& painter,
                               const QRectF& preceding_bar_rect,
                               qreal num_center_y) const;
    // Paints all volta brackets on a single line.  Bracket positions are
    // derived from the per-bar rects in `line` plus the precomputed
    // line.volta_spans; the bracket sits just above the line at a fixed
    // vertical offset (see k_volta_zone_height) reserved by
    // compute_layout when any bar on the line carries a volta number.
    void paint_volta_brackets(QPainter& painter,
                              const line_layout& line) const;
    // Paints a row of filled dots for a bar whose number_of_beats differs from
    // the song time signature.  beat_count dots are drawn, centred
    // horizontally over the bar's chord column.  line_has_articulation selects
    // where they sit vertically: above bl.rect in the reserved zone when the
    // line carries articulations, otherwise inside the bar's own top padding
    // (see beat_dot_zone_height).
    void paint_beat_dots(QPainter& painter,
                         const bar_layout& bl,
                         qreal headroom) const;

    // Returns a per-song-bar pair {draw_begin_repeat, draw_end_repeat}
    // derived from each bar's repeat() flag PLUS the implicit end-repeat
    // sign at the right edge of every non-final volta.  Called once per
    // layout pass, before per-line geometry is computed, because the
    // analysis is global (repeat sections, and the volta groups within
    // them, can span multiple visual lines).
    //
    // The "final volta" of a repeat section is the volta span containing
    // the highest volta number used in that section.  In well-formed
    // input that's the last (rightmost) span; pathological inputs where
    // a high-numbered volta appears before a low-numbered one still get
    // a sensible answer — the higher number wins, matching the playback
    // semantics expected by readers.
    std::vector<std::pair<bool, bool>> compute_repeat_flags() const;

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

    // Pops up the bar context menu (Repeat submenu + Voltas...) anchored
    // at `global_pos`.  Shared between contextMenuEvent (right-click on
    // a bar) and any other path that wants the same affordance.  The
    // menu's actions operate on the current selection, so callers are
    // responsible for ensuring the selection is correct before calling.
    void show_bar_context_menu(const QPoint& global_pos);

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

    // Constrain an inline bar editor's rect to the content column so it
    // stays fully visible.  Bar editors are widened past their bar for
    // typing room (see edit_bar / edit_new_bar); for a bar near the right
    // edge that widening would otherwise push the editor off the page,
    // hiding the text as it's typed.  Shifts the rect left to sit within
    // the printable content band (and clamps its width if the band is
    // narrower than the requested editor), leaving y untouched.
    QRectF clamp_bar_editor_rect(QRectF r) const;

    // Multi-line counterpart for annotation text boxes.  Differs from
    // open_line_editor in three ways:
    //   1. Uses QPlainTextEdit so Enter inserts a newline instead of
    //      committing.  This matches Google Drawings: the only ways
    //      to end editing are clicking outside the box or pressing
    //      Esc (which still cancels).
    //   2. Grows the editor vertically as the user types past the
    //      initial rect's height, and writes the new height back to
    //      the annotation on commit — text boxes expand to fit
    //      content, again mirroring Google's behavior.
    //   3. Captures the text-box id so the grow-on-type and commit
    //      paths know which annotation to mutate.  The id is held in
    //      editing_text_box_id_ for the editor's lifetime.
    // Same commit contract: returns trimmed text via the callback;
    // callback returning false silently reverts.  No placeholder
    // because annotation editors don't show one — Google doesn't.
    void open_multiline_editor(const QRectF& rect,
                               const QString& initial,
                               std::uint64_t text_box_id,
                               std::function<bool(const QString&)> commit);
    // Returns true iff the commit callback was both invoked AND returned
    // true.  Esc/cancel and validation-rejected commits both return
    // false.  The return is consulted by the Tab-advance path so a
    // rejected commit doesn't silently move the editor onto a different
    // bar — the user would lose their input without realising it.
    bool close_line_editor(bool commit_value);

    // Active inline editor.  Either a QLineEdit (single-line: title,
    // key, time-sig, tempo, sections, bars) or a QPlainTextEdit
    // (multi-line: annotation text boxes).  We hold it as the QWidget
    // base because close_line_editor reads text via dynamic_cast on
    // the actual type — both editors store their string differently
    // (text() vs. toPlainText()) and require slightly different setup.
    // editingFinished is QLineEdit-only, so the multi-line editor has
    // its own focus-out hook (see open_multiline_editor for the
    // wiring).
    QWidget* active_editor_ = nullptr;
    std::function<bool(const QString&)> editor_commit_;

    // The text box being edited by the multi-line editor, if any.
    // Used by the live "grow box to fit text" feedback so we can apply
    // height changes to the underlying annotation as the user types.
    // nullopt for any editor type other than the text-box one.
    std::optional<std::uint64_t> editing_text_box_id_;

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
    // True while the cursor is within the divider's grab zone (but not yet
    // dragging).  Drives paint_divider to reveal the faint full-height
    // guide line and darken the grabber; cleared on leave.
    bool hovered_divider_   = false;

    // --- Bar drag-to-reorder ---
    // A plain press on a bar always arms a potential drag (drag_armed_)
    // without necessarily collapsing an existing multi-bar selection —
    // if the pressed bar is already selected, we defer the "click just
    // selects this one bar" behaviour to mouseReleaseEvent so that
    // pressing-then-dragging an existing multi-selection moves the whole
    // thing, matching the usual file-manager convention.  Once the
    // cursor moves past Qt's drag-start threshold, drag_armed_ promotes
    // to dragging_bars_ and every subsequent move re-resolves
    // drag_drop_target_ via compute_drop_target (nullopt when over empty
    // space or one of the dragged bars themselves).  Release commits the
    // move via move_selected_bars; Esc cancels without moving anything.
    bool                        drag_armed_       = false;
    bool                        dragging_bars_    = false;
    std::size_t                 drag_press_bar_   = 0;
    QPoint                      drag_press_pos_;

    // A resolved drop location.  insert_at is a flat song::bars() index
    // in [0, bars().size()] — the position to insert the dragged block
    // before (bars().size() itself means "append at the very end").
    // indicator_rect is whatever rect (an existing bar's, or an
    // insertion slot's ghost rect) the drop-point indicator should hug
    // the left edge of.  force_new_line is set only for the "append a
    // new line" slot at the end of the song: it means the bar
    // immediately before insert_at must be given is_eol = true (if it
    // doesn't already have it) so the dropped block actually starts a
    // fresh line rather than continuing the last one.  extends_line is
    // set only for a same_line slot target: it means insert_at is "the
    // end of a specific line" rather than "immediately before whatever
    // bar happens to live at this flat index" — which matters because
    // when that line isn't the song's last, insert_at numerically lands
    // on the *next* line's first bar, and naively treating that as an
    // ordinary "insert before this bar" both misattributes which line
    // the drop belongs to and — because the target line's old last bar
    // keeps its is_eol marker exactly where it was — leaves the dropped
    // block stranded after that boundary, i.e. it starts the next line
    // instead of actually extending this one.  See move_selected_bars.
    struct bar_drop_target
    {
        std::size_t insert_at      = 0;
        bool        force_new_line = false;
        bool        extends_line   = false;
        QRectF      indicator_rect;
    };
    std::optional<bar_drop_target> drag_drop_target_;

    // Resolves the drop target under point `p`, or nullopt if there
    // isn't a valid one there.  Checks, in order: an existing bar (not
    // part of the current selection) — drop before it; a same_line
    // insertion slot — drop at the end of that line; a next_line
    // insertion slot — drop as a new line at the end of the song.
    // first_bar slots (empty song) never produce a target since there's
    // nothing to have dragged in the first place.
    std::optional<bar_drop_target> compute_drop_target(const QPointF& p) const;

    // Returns the flat index into song_.bars() belonging to the same
    // "line" as bar `flat_idx`, where a line is the run of bars ending
    // at (and including) the next bar with is_eol() set, or the end of
    // the song.  Used by move_selected_bars to decide whether a drop
    // target shares the dragged bars' original line.
    std::vector<std::size_t> line_index_of_each_bar() const;

    // Moves the currently-selected bars (selected_bars_) so they land,
    // as a contiguous block in their original relative order, at
    // `insert_at` (a flat song::bars() index; bars().size() appends at
    // the very end).  No-op if nothing is selected, insert_at is out of
    // range, or insert_at points at one of the selected bars themselves.
    //
    // is_eol handling: every dragged bar's is_eol is unconditionally
    // cleared first — is_eol marks "the last bar of a line," and a
    // dragged bar essentially never keeps that role at its new
    // position, whether the drop lands within its own original line or
    // a different one.  From there:
    //   * extends_line (and not force_new_line): insert_at is "the end
    //     of a specific line," which may numerically coincide with the
    //     next line's first bar's index — so if that line's old last
    //     bar explicitly has is_eol = true, that flag is moved to the
    //     end of the dragged block instead (and cleared from the old
    //     bar, if it survives here rather than being dragged away
    //     itself).  This is what makes the block actually extend the
    //     line rather than start a new one immediately after it — and
    //     it applies even when there's no next line yet, since is_eol
    //     on a line's current last bar becomes a real boundary the
    //     moment anything is inserted after it.
    //   * force_new_line: the bar immediately before insert_at (if any
    //     survives there) is given is_eol = true, so the block starts a
    //     genuinely new line.
    //   * plain insert-before-an-existing-bar (neither flag set): no
    //     further action — the dragged block simply merges into
    //     whatever line insert_at falls into.
    //   * In every case, if the bars dragged away included their
    //     original line's terminal (is_eol) bar and that line isn't the
    //     one being extended above, that flag is transferred to
    //     whichever bar is now last among that line's survivors, so the
    //     vacated line still ends in a sensible place instead of
    //     silently merging with whatever used to follow it.
    void move_selected_bars(std::size_t insert_at, bool force_new_line, bool extends_line);

    // Paints the vertical insertion-point indicator at the left edge of
    // drag_drop_target_'s indicator_rect while a bar drag is in
    // progress.  No-op when not dragging or no valid target is hovered.
    void paint_drag_indicator(QPainter& painter) const;

    // --- Constants ---
    // Padding above and below the title text.  Kept tight: a Nashville chart
    // treats vertical space as a primary resource, so the title hugs the top
    // of the page and sits close to the chart body rather than floating in a
    // large band of whitespace.
    static constexpr qreal k_title_padding       = 6.0;   // above and below title text
    // Base point size of the title text at scale 1.0.  Used for BOTH the
    // painted title and the reserved title-band height so the two never
    // drift (they previously measured at one size and painted at another).
    // Multiplied by the live font scale at use sites.
    static constexpr qreal k_title_base_pt       = 12.0;
    // Vertical gap between the title block and the first chart line.  This is
    // deliberately separate from k_content_padding (which governs the left/
    // right/bottom content insets): the title already carries its own bottom
    // padding, so reusing the full content padding here double-padded the gap.
    static constexpr qreal k_title_content_gap   = 4.0;
    static constexpr qreal k_line_spacing        = 11.0;  // spacing after a section-end rule
    static constexpr qreal k_line_spacing_normal = 6.0;   // uniform spacing between all other lines
    static constexpr qreal k_inter_bar_spacing   = 6.0;
    // Horizontal padding baked into every bar's own reserved column width,
    // beyond what its content (chords, time sig, parens, etc.) strictly
    // needs.  Gives a bar's trailing edge natural breathing room before
    // whatever comes next.
    static constexpr qreal k_bar_padding         = 16.0;
    // Vertical zone reserved above the bar row when a line carries any
    // volta numbers.  Houses the labelled volta bracket (label text + a
    // small downward hook).  Lines without voltas don't reserve this so
    // the rest of the chart packs as densely as before.
    static constexpr qreal k_volta_zone_height   = 18.0;
    // Radius of a single beat dot, and the clear space between the bottom of
    // the dot row and the top of the bar rect below it.  The zone height is
    // derived from these two rather than set independently, so the space
    // reserved and the space drawn into can't drift apart.
    static constexpr qreal k_beat_dot_radius     = 1.1;
    static constexpr qreal k_beat_dot_gap        = 3.0;
    // Vertical zone above a bar's rect when that bar has a custom beat count.
    // Holds a row of filled dots (one per beat) centred horizontally over the
    // bar's chord column and all articulations above it.  The dots are
    // bottom-aligned in this zone — hung just above the bar — rather than
    // centred in it: centring in a zone sized well above the dots' own extent
    // pushed them away from the chords they annotate and spent the rest of the
    // zone on nothing, which compounds badly on a chart with custom beats on
    // every line.  The zone is now exactly the dot row plus its gap.
    static constexpr qreal k_beat_dot_zone_height =
        2.0 * k_beat_dot_radius + k_beat_dot_gap;

    // The zone is only reserved when the line ALSO carries articulations.
    // With no articulations the bar already has top padding above its chord
    // numbers (bar_renderer's top_pad plus chord_renderer's k_plain_top_pad)
    // that nothing occupies, and the dot row fits inside it — so reserving a
    // separate strip there both pushed the dots away from the chords they
    // annotate and cost a line's worth of height per page.  When there ARE
    // articulations that padding is real ink, so the strip is still needed.
    // Every site that reserves or consumes the zone must go through this, or
    // the dots and the space allowed for them drift apart.
    // Which above-number articulations occur anywhere on a line.  `both` means
    // some single chord is BOTH staccato and pushed, which is the arrangement
    // that reaches highest into the articulation zone.
    struct art_kinds
    {
        bool staccato = false;
        bool pushed   = false;
        bool tied     = false;
        bool both     = false;
        bool any() const { return staccato || pushed || tied; }
    };
    static art_kinds scan_articulations(const std::vector<const model::bar*>& bars);

    // Clear vertical space above a line's topmost painted content, measured
    // down from the top of the bar rect.  This is where the beat-dot row goes.
    // It is the bar's own top padding plus, on a line with articulations, the
    // empty upper part of the articulation zone (articulations are anchored to
    // that zone's floor — see chord_renderer::articulation_headroom).
    qreal beat_dot_headroom(const art_kinds& kinds, bool is_duration_mode) const;

    // Height that must be reserved ABOVE the bar rect for the dot row: only
    // whatever the headroom can't already absorb.  Lines with enough slack
    // above their content reserve nothing at all.
    static qreal beat_dot_zone_height(bool has_beat_dots, qreal headroom)
    {
        if (!has_beat_dots)
            return 0.0;
        const qreal needed = 2.0 * k_beat_dot_radius + k_beat_dot_gap;
        return needed > headroom ? needed - headroom : 0.0;
    }
    static constexpr qreal k_content_padding    = 12.0;
    static constexpr int   k_divider_hit_width  = 5;
    // Geometry of the always-visible margin-resize grabber drawn at the top
    // of the divider boundary.  A small rounded handle that signals "drag
    // to resize the margin" without painting a full-height rule (which read
    // as a table border, especially on print).
    static constexpr qreal k_divider_grabber_w  = 6.0;
    static constexpr qreal k_divider_grabber_h  = 22.0;
    static constexpr int   k_min_margin_width   = 60;
    static constexpr int   k_max_margin_width   = 200;
    static constexpr int   k_default_margin_width = 100;

    // --- Bar-grid font base sizes ---
    // Point sizes (at font_scale_ == 1.0) for the fonts that render the bar
    // grid: the dominant chord number and its tuned satellites.  Named here
    // so build_fonts() and text_box_font() share one source of truth instead
    // of repeating the literals.
    static constexpr qreal k_number_base_pt       = 15.0;
    static constexpr qreal k_modifier_base_pt      =  9.0;
    static constexpr qreal k_articulation_base_pt  = 10.0;
    static constexpr qreal k_music_base_pt         = 11.5;

    // Absolute readability floor on the *effective* grid scale
    // (font_scale_ * grid_fit_scale_).  A chart wide enough that even
    // shrinking to this won't fit the content column stops shrinking here —
    // past this the font would be illegible, so we let the overflow stand
    // rather than render a micro-font.  0.4 == "40% of the shipped base
    // size", independent of the user's Text size (the fit search divides this
    // by font_scale_ to bound the effective scale, not the multiplier).
    static constexpr qreal k_min_fit_scale         = 0.4;

    // Auto-fit binary search: how many halving steps to run when a line has
    // to be shrunk to fit, and how far inside content_w to aim.  12 steps
    // resolves the scale to well under a pixel of line width; the 1px safety
    // margin keeps sub-pixel rounding from tipping the rendered line back
    // over the content edge.
    static constexpr int   k_fit_search_iterations = 12;
    static constexpr qreal k_fit_safety_margin     = 1.0;

    // Placeholder text shown when title is empty (in both painted form
    // and the inline editor's QLineEdit).
    static constexpr const char* k_title_placeholder = "Title";

    // --- Data ---
    model::song&             song_;
    std::vector<line_layout> lines_;
    chord_renderer::Fonts    fonts_;
    // Multiplier applied to every chart font.  Seeded from ui_settings on
    // construction and updated via apply_font_scale.  1.0 == shipped sizes.
    qreal                    font_scale_   = 1.0;
    // Automatic horizontal-fit multiplier applied ON TOP OF font_scale_ to
    // the bar-grid fonts only.  Recomputed from scratch on every layout so
    // the widest line stays within the content column instead of spilling
    // off the page: 1.0 when the chart already fits, smaller when a line has
    // to be shrunk to fit.  Purely derived layout state, never persisted —
    // font_scale_ is the authored size; this only ever shrinks the grid.
    // The title, the left margin gutter, and free-text annotations are
    // deliberately excluded so they keep their authored size regardless.
    qreal                    grid_fit_scale_ = 1.0;
    // Independent font multipliers for the two elements outside the bar
    // grid: the song title and the left margin gutter (key / time / tempo).
    // Seeded from ui_settings on construction, changed via apply_title_scale
    // / apply_margin_scale.  1.0 == shipped sizes.  Kept separate from
    // font_scale_ so the user can size these without disturbing the chart
    // body (and vice-versa).
    qreal                    title_scale_  = 1.0;
    qreal                    margin_scale_ = 1.0;
    // Bravura (music) font family, resolved once and cached so re-running
    // init_fonts() on a scale change doesn't repeatedly addApplicationFont.
    QString                  music_family_;
    int                      margin_width_ = k_default_margin_width;
    // App-wide page size/margins (from ui_settings), seeded on construction
    // and refreshed via apply_page_geometry.  Drives both where page breaks
    // fall (compute_layout) and how many pages the widget currently spans.
    page_geometry            page_geo_;
    int                      page_count_   = 1;
    modal_overlay*           overlay_      = nullptr;  // non-owning; set by main_window

    // The annotation overlay.  Order matters: constructed in the
    // initialiser list of the constructor AFTER song_ (because it
    // captures song_.annotations() by reference).  It also captures
    // lines_ and a lambda that re-derives the chart-content rect on
    // demand; see the constructor.
    annotation_layer annotation_layer_;

    // Open the inline editor over a text box and, on commit, store the
    // new text into the annotation by id.  Mirrors edit_bar's pattern
    // (capture id, look up freshly on commit, no in-flight pointers).
    void edit_text_box(std::uint64_t text_box_id, const QRectF& widget_rect);

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
    // The actual bar removal, factored out of delete_selection so the
    // "this would empty the song" case can gate it behind a confirmation
    // (see delete_selection) while the ordinary case calls it directly.
    void perform_delete_selection();
    void paste_clipboard();     // insert clipboard after last selected bar
                                // (or at song end if no selection)

    // --- Undo / redo ---
    // Value-snapshot history rather than a command pattern: songs are
    // small (a handful of bars/attributes), so copying the whole
    // model::song on every edit is cheap, and it means every mutation
    // site needs one line (push_undo_snapshot()) instead of a
    // hand-written inverse. Snapshots are plain data copies, never
    // installed as anyone's live song — same idea as song_tab's
    // last_saved_ baseline.
    //
    // Deliberately NOT covered yet: annotation edits (text boxes /
    // connectors). Those mutate model::annotations directly through
    // annotation_layer's live reference rather than through a
    // song_-level setter, so they need their own hook points; folding
    // them in is a follow-up, not part of this pass.
    //
    // Capped so a very long editing session can't grow this
    // unboundedly; dropping the oldest entry when full just narrows
    // how far back undo can reach, which is an acceptable trade for
    // bounded memory.
    static constexpr std::size_t k_max_undo_depth = 200;
    std::vector<model::song> undo_stack_;
    std::vector<model::song> redo_stack_;

    // Snapshots the current song_ onto undo_stack_ and clears
    // redo_stack_ (a fresh edit invalidates whatever redo history
    // existed). Called at the start of every mutating operation in
    // scope for undo — right after that operation's own "would this
    // be a no-op" guards, so cancelled edits (empty input, parse
    // failure, no actual change) never push a dead snapshot.
    void push_undo_snapshot();

    // Applies every content field a snapshot carries onto the live
    // song_ via its normal setters (bars(), annotes().load(), key(),
    // tempo(), time_sig(), bars_per_line(), margin_width(), name(),
    // meta()) — never by assigning *song_ wholesale. A whole-object
    // assignment would also overwrite annotations_'s view-installed
    // anchor_resolver (a std::function captured against `this`), and
    // while that's usually harmless (later snapshots carry a copy of
    // the same live resolver), an early snapshot taken before the
    // resolver was installed would silently null it out. Routing
    // through the setters — same as annotations::load() already does
    // for the database's load path — sidesteps that regardless of
    // which snapshot gets restored. Does not touch selection.
    void restore_snapshot(const model::song& snap);

    void init_fonts();
    // Build the bar-grid fonts (fonts_) at the given effective point-size
    // scale.  init_fonts() calls this with font_scale_ (the authored size);
    // compute_layout drives it with font_scale_ * candidate while searching
    // for a horizontal fit, so fonts_ ends a layout holding the shrunk grid
    // fonts that the paint pass then draws with.
    void build_fonts(qreal effective_scale);
    // Resolve and cache the Bravura music-font family (member music_family_).
    // Called once, lazily, from init_fonts.
    void resolve_music_family();
    // The font used for text-box annotations: the chord-number font at 75%,
    // floored at 8pt.  Shared by construction, the inline editor, and
    // apply_font_scale so all three stay in sync.
    QFont text_box_font() const;
    // The left-margin width for a given scale: the default width scaled and
    // clamped to [k_min_margin_width, k_max_margin_width].
    int scaled_margin_default(qreal scale) const;
};

} // namespace nashville::view
