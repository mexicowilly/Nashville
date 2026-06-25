#include "song_body_widget.hpp"
#include "margin_renderer.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QKeySequence>
#include <stdexcept>
#include <cmath>
#include <map>

namespace nashville::view
{

// Color used for placeholder title text.
static const QColor k_placeholder_color(150, 150, 150);

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
song_body_widget::song_body_widget(model::song& song, QWidget* parent)
    : QWidget(parent),
      song_(song),
      // annotation_layer_ must follow song_ in the init list because it
      // captures song_.annotations() by reference.  The chart-content
      // rect supplier re-derives the rect on each call using current
      // widget metrics — the same expression rebuild() uses below — so
      // we automatically track resize and margin-drag without any
      // signal/slot plumbing.
      annotation_layer_(
          song.annotes(),
          lines_,
          [this]() {
              const qreal th = title_height();
              return QRectF(
                  margin_width_ + k_content_padding,
                  th + k_content_padding,
                  std::max(0.0, width()  - margin_width_ - k_content_padding * 2),
                  std::max(0.0, height() - th - k_content_padding * 2));
          })
{
    setMouseTracking(true);
    // StrongFocus so the widget receives key events for Esc/clipboard
    // shortcuts.  We also call setFocus() on every mouse press below so
    // that clicking the chart hands keyboard focus back from any sibling
    // widget (toolbar, sidebar) the user may have last interacted with.
    setFocusPolicy(Qt::StrongFocus);

    // Force a white-paper / black-ink palette regardless of the
    // desktop theme.  The chart is meant to imply a printed page, so
    // even on a dark-themed system the widget reads as paper.
    // setAutoFillBackground + setting Window/Base to white covers the
    // brief moment Qt clears the widget before paintEvent runs (where
    // it would otherwise use the system's window color).  Child
    // widgets — inline editors — pick up the same colors via the
    // explicit apply_print_palette call in open_line_editor /
    // open_multiline_editor; we don't rely on inheritance here
    // because Qt's QPalette inheritance is partial and theme-
    // dependent (some palette roles propagate, others don't).
    {
        QPalette pal = palette();
        pal.setColor(QPalette::Window,     Qt::white);
        pal.setColor(QPalette::Base,       Qt::white);
        pal.setColor(QPalette::WindowText, Qt::black);
        pal.setColor(QPalette::Text,       Qt::black);
        setPalette(pal);
        setAutoFillBackground(true);
    }

    init_fonts();

    // Wire the annotation layer's text-editing requests through to our
    // existing inline-editor plumbing.  We don't give the layer its own
    // QLineEdit — sharing the widget's lets Esc/focus/Tab behaviour stay
    // consistent across every editable surface in the chart.
    annotation_layer_.set_edit_text_callback(
        [this](std::uint64_t id, const QRectF& rect) {
            edit_text_box(id, rect);
        });
    // Pick a font for text-box contents that reads as "annotative" next
    // to the chart: smaller than the chord-number font but in the same
    // family, so annotations look native rather than imported.  75%
    // size matches the section-label convention elsewhere; clamp at 8pt
    // because Qt's font hinting falls apart below that.
    QFont tb_font = fonts_.number;
    tb_font.setPointSize(std::max(8, int(tb_font.pointSize() * 0.75)));
    annotation_layer_.set_text_font(tb_font);

    // Install an anchor resolver on the annotations model.  The model
    // needs this when a text box is removed: any connector endpoints
    // glued to that box are converted to free endpoints at the
    // anchor's last-known position, which requires the same anchor
    // geometry the view uses for painting.  We compute the position
    // in song-local coords (because annotation endpoints store song-
    // local positions when free), but the resolver runs through the
    // view's text_box_anchors() helper which returns widget coords,
    // so we sub off the chart-content origin to convert.
    song.annotes().set_anchor_resolver(
        [this](std::uint64_t tb_id, unsigned idx) -> QPointF {
            const auto* tb = song_.annotes().find_text_box(tb_id);
            if (!tb || idx >= 8)
                return QPointF(0, 0);
            // Compute the anchor in song-local coords directly from
            // tb->rect: same 8-point formula as text_box_anchors but
            // in song space, no widget-coord conversion needed.
            const QRectF& r = tb->rect;
            const qreal cx = r.center().x(), cy = r.center().y();
            switch (idx)
            {
            case 0: return QPointF(r.left(),  r.top());     // NW
            case 1: return QPointF(cx,        r.top());     // N
            case 2: return QPointF(r.right(), r.top());     // NE
            case 3: return QPointF(r.right(), cy);          // E
            case 4: return QPointF(r.right(), r.bottom()); // SE
            case 5: return QPointF(cx,        r.bottom()); // S
            case 6: return QPointF(r.left(),  r.bottom()); // SW
            case 7: return QPointF(r.left(),  cy);          // W
            }
            return QPointF(0, 0);
        });

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
// compute_repeat_flags
// ---------------------------------------------------------------------------
// Walks the song's bars once to determine which bars should paint a
// begin-repeat sign at their left edge and which should paint an
// end-repeat at their right.  Two sources contribute:
//   1. The model's bar::repeat() flag (BEGIN / END / NONE) — explicit
//      author intent, painted verbatim.
//   2. Voltas — every non-final volta within a repeat section ends with
//      an *implicit* end-repeat, even when the bar's repeat() is NONE.
//      This matches standard music-notation practice (and the spec the
//      user laid out): the player loops back after each non-final
//      ending, then falls through after the final one.
//
// "Repeat section": the half-open span from a BEGIN bar up through the
// matching END bar.  Sections may NEST — an inner BEGIN inside an
// already-open section pushes a new section without closing the outer,
// and the matching inner END pops back to the outer.  A volta belongs
// to its INNERMOST enclosing section: whether its bracket needs an
// implicit end-repeat depends only on what other voltas appear inside
// that same innermost section, not on any outer enclosing section.
//
// "Final volta of a section": the volta span(s) within that section
// containing the highest volta *number* used anywhere in the section.
// Non-final spans get an implicit end-repeat at their last bar; final
// spans do not.  In well-formed input the final span is also the
// rightmost; even if a user inverts the order (a {2} span before a
// {1} span), the {2} span still wins — playback semantics readers
// expect, regardless of authoring order.
//
// Implementation: a stack of "open section" keys, where each key is
// the index of that section's BEGIN bar (uniquely identifying it
// even across nesting and re-entry).  Voltas are tagged with the
// stack's top at the time they're seen; final-volta determination
// then groups by that key.
std::vector<std::pair<bool, bool>> song_body_widget::compute_repeat_flags() const
{
    const auto& bars = song_.bars();
    std::vector<std::pair<bool, bool>> flags(bars.size(), {false, false});

    // Step 1: explicit repeat flags from the model.
    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        if (bars[i].repeat() == model::bar::repeat_status::BEGIN)
            flags[i].first = true;
        else if (bars[i].repeat() == model::bar::repeat_status::END)
            flags[i].second = true;
    }

    // Step 2: collect contiguous volta spans across the whole song,
    // each tagged with the section_key of its innermost enclosing
    // repeat section.
    struct global_span
    {
        std::size_t first_bar_index = 0;
        std::size_t last_bar_index  = 0;
        unsigned    max_number      = 0;
        std::size_t section_key     = 0;
    };
    std::vector<global_span> spans;

    auto max_in = [](const std::set<unsigned>& s) -> unsigned {
        // set<unsigned> sorts ascending, so the last element is the max.
        // Empty handled by caller; we never call this on empty sets.
        return *s.rbegin();
    };

    // Open-section stack.  Pushed on BEGIN, popped on END.  A sentinel
    // value distinct from any real bar index is needed so we can detect
    // "no section open" without the stack being empty — handy because
    // the UI guarantees voltas only appear inside an open section, so
    // hitting the sentinel is a sign of malformed input, not a normal
    // path.  We use static_cast<size_t>(-1) since real bar indices are
    // non-negative and < bars.size().
    constexpr std::size_t k_no_section = static_cast<std::size_t>(-1);
    std::vector<std::size_t> section_stack;

    auto current_section = [&]() -> std::size_t {
        return section_stack.empty() ? k_no_section : section_stack.back();
    };

    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        // Section-stack updates must precede volta accumulation for
        // BEGIN (the new section is in effect *at* its own bar) but
        // must follow it for END (the closing bar can itself carry a
        // volta belonging to the section it closes — common when the
        // last volta is a single-bar ending that also bears the END
        // mark).
        if (bars[i].repeat() == model::bar::repeat_status::BEGIN)
            section_stack.push_back(i);

        const auto& vs = bars[i].voltas();
        if (!vs.empty())
        {
            std::size_t key = current_section();

            // Extend the previous span if this bar is adjacent, shares
            // the same volta set, AND lives in the same section.  The
            // section check matters under nesting: two voltas with
            // identical numbers in different sections must NOT merge.
            // (Without nesting it's also still required — sections
            // separated by an END/BEGIN pair could otherwise collapse.)
            if (!spans.empty()
                && spans.back().last_bar_index + 1 == i
                && bars[spans.back().last_bar_index].voltas() == vs
                && spans.back().section_key == key)
            {
                spans.back().last_bar_index = i;
                spans.back().max_number = std::max(spans.back().max_number,
                                                  max_in(vs));
            }
            else
            {
                global_span gs;
                gs.first_bar_index = i;
                gs.last_bar_index  = i;
                gs.max_number      = max_in(vs);
                gs.section_key     = key;
                spans.push_back(gs);
            }
        }

        if (bars[i].repeat() == model::bar::repeat_status::END)
        {
            // Pop the innermost section.  If the stack is empty here,
            // the input has more ENDs than BEGINs — defensively ignore
            // the extra END's effect on section bookkeeping (its visual
            // mark from step 1 still paints), since there's nothing
            // sensible to pop.
            if (!section_stack.empty())
                section_stack.pop_back();
        }
    }

    // Step 3: per section, find the max volta number across all spans
    // in that section.  Any span whose own max_number equals that
    // section-wide max is "final" and does NOT carry an implicit end-
    // repeat.  Every other span ends with one.
    //
    // Spans tagged k_no_section (malformed input — voltas with no
    // enclosing BEGIN) participate normally; they form their own
    // synthetic "section" keyed by k_no_section, and the same
    // final-volta logic applies.  The UI guarantees this won't happen
    // in practice, but the logic stays well-defined regardless.
    std::map<std::size_t, unsigned> section_max;
    for (const auto& s : spans)
    {
        auto it = section_max.find(s.section_key);
        if (it == section_max.end() || it->second < s.max_number)
            section_max[s.section_key] = s.max_number;
    }

    for (const auto& s : spans)
    {
        unsigned section_top = section_max[s.section_key];
        bool is_final = (s.max_number == section_top);
        if (!is_final)
        {
            // Implicit end-repeat at the last bar of this non-final
            // volta span.  Additive — an explicit END flag on the same
            // bar is preserved; we never *clear* a model-sourced flag
            // here.  When the same bar already has draw_end_repeat
            // from step 1, this is a no-op; the visual outcome is one
            // repeat mark either way, which matches the convention of
            // not stacking two repeat signs at the same location even
            // when conceptually two loops close together.
            if (s.last_bar_index < flags.size())
                flags[s.last_bar_index].second = true;
        }
    }

    return flags;
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

    // Repeat flags (one entry per bar in song order).  Computed once
    // up front because the analysis spans multiple visual lines: a
    // repeat section's "final volta" determination needs the whole
    // section in view, and the section may start on line N and end on
    // line N+M.  Indexed by flat song-bar index.
    const auto repeat_flags = compute_repeat_flags();

    // --- Pass 1: group bars into lines ---
    // We also stash the flat song index of each bar so per-bar layout
    // below can consult repeat_flags without re-deriving "which bar of
    // the song is this?" via a second walk.
    struct raw_bar  { const model::bar* bar; std::size_t song_index; };
    struct raw_line { std::vector<raw_bar> bars; };
    std::vector<raw_line> raw_lines;
    raw_line current;

    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        current.bars.push_back({&bars[i], i});

        if (bars[i].is_eol())
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
        const auto* first = raw.bars.front().bar;
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
    auto line_has_articulation = [](const std::vector<raw_bar>& bars) {
        for (const auto& rb : bars)
            for (const auto& ch : rb.bar->chords())
                if (ch.is_pushed() || ch.is_staccato() || ch.is_tied())
                    return true;
        return false;
    };

    // Helper: does any bar on this line carry a volta number?  When true,
    // compute_layout reserves k_volta_zone_height above the chord row so
    // the volta bracket has somewhere to sit without overlapping the
    // chords.
    auto line_has_voltas = [](const std::vector<raw_bar>& bars) {
        for (const auto& rb : bars)
            if (!rb.bar->voltas().empty())
                return true;
        return false;
    };

    auto line_bar_height = [&](const std::vector<raw_bar>& bars) -> qreal {
        bool has_art = line_has_articulation(bars);
        for (const auto& rb : bars)
            if (bar_renderer::is_duration_mode(*rb.bar))
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
            // Width must include the repeat-mark slots when present:
            // they sit outside the chord row and consume real horizontal
            // space.  Looking up the flags by song_index keeps this in
            // sync with what paint() will eventually draw.
            const auto& rb = raw.bars[j];
            bool begin_r = repeat_flags[rb.song_index].first;
            bool end_r   = repeat_flags[rb.song_index].second;
            qreal w = bar_renderer::width_hint(*rb.bar, bar_h, fonts_,
                                              begin_r, end_r) + k_bar_padding;
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
            && raw_lines[i].bars.front().bar->section().has_value();
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
        line.has_voltas       = line_has_voltas(raw.bars);

        // Reserve a volta zone above the bar row when needed.  The bars
        // themselves get pushed down by this amount; line.rect covers
        // the whole stack (volta zone + bars) so callers that need a
        // line-bounding rect (insertion-slot placement, section-end
        // rules, hit-testing whitespace) see the true vertical extent.
        qreal volta_zone_h = line.has_voltas ? k_volta_zone_height : 0.0;
        qreal bar_top_y    = y + volta_zone_h;

        // Section label comes from the first bar only.  Sections on
        // non-first bars are silently ignored — see the UI invariant.
        if (!raw.bars.empty() && raw.bars.front().bar->section())
            line.section_label =
                QString::fromStdString(*raw.bars.front().bar->section());

        // Section column: full bar height, left-aligned within content_rect.
        // Width is the label box only (without the gap).
        qreal box_w = section_col_w > 0.0 ? section_col_w - k_section_gap : 0.0;
        // The section label should centre on the number row, not the full bar height.
        // Number row starts at: top_pad(2px) + art_zone (if any) + k_plain_top_pad(4px).
        // Offset by the volta zone so the label tracks the chord row
        // when a bracket pushes the bars down.
        constexpr qreal k_bar_top_pad   = 2.0;   // matches bar_renderer fixed top_pad
        constexpr qreal k_plain_top_pad = 4.0;   // matches chord_renderer k_plain_top_pad
        qreal art_zone = line.has_articulation
            ? [&]{ QFontMetricsF a(fonts_.articulation); return a.ascent() + a.descent(); }()
            : 0.0;
        qreal num_top = bar_top_y + k_bar_top_pad + (art_zone > 0.0 ? art_zone : k_plain_top_pad);
        QFontMetricsF num_fm(fonts_.number);
        qreal num_h   = num_fm.ascent() + num_fm.descent();
        line.section_col_rect = QRectF(content_rect.left(), num_top, box_w, num_h);

        qreal x = bars_left;
        for (std::size_t j = 0; j < raw.bars.size(); ++j)
        {
            const auto& rb = raw.bars[j];
            const model::bar* b = rb.bar;
            qreal bar_w = col_widths[j];

            bar_layout bl;
            bl.bar              = b;
            bl.rect             = QRectF(x, bar_top_y, bar_w, actual_bar_h);
            bl.is_duration_mode = bar_renderer::is_duration_mode(*b);
            bl.draw_begin_repeat = repeat_flags[rb.song_index].first;
            bl.draw_end_repeat   = repeat_flags[rb.song_index].second;

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

        // --- Volta spans for this line ---
        // Group consecutive bars whose voltas() sets are equal and
        // non-empty.  Adjacency alone isn't sufficient under nested or
        // back-to-back repeats: two bars can be adjacent on screen and
        // carry the same volta numbers yet belong to different repeat
        // sections (e.g. an END bar followed by a BEGIN bar that also
        // has its own volta).  We respect the section boundary by
        // refusing to extend across a BEGIN (which opens a new section
        // at the current bar) or across a previous-bar END (which
        // closed the prior section, putting the current bar into a
        // different one).  This mirrors the section-key check used by
        // compute_repeat_flags for the same reason.
        //
        // The closed/open flag mirrors the implicit-end-repeat decision
        // made by compute_repeat_flags: a span is closed iff its last
        // bar carries draw_end_repeat (either explicit from the model
        // or implicit from being a non-final volta).  This keeps the
        // bracket hook and the repeat-sign dots in perfect agreement
        // — there's exactly one source of truth for "is this a closing
        // volta?", consulted in two visual forms.
        if (line.has_voltas)
        {
            for (std::size_t j = 0; j < raw.bars.size(); ++j)
            {
                const auto& vs = raw.bars[j].bar->voltas();
                if (vs.empty()) continue;

                bool can_extend = false;
                if (!line.volta_spans.empty()
                    && line.volta_spans.back().last_bar_index + 1 == j
                    && raw.bars[line.volta_spans.back().last_bar_index]
                           .bar->voltas() == vs)
                {
                    // Same volta set and adjacent; now verify no
                    // section boundary lies between the previous bar
                    // and this one.  draw_begin_repeat on j means a new
                    // section starts here; draw_end_repeat on j-1
                    // means the previous section closed there.
                    bool starts_new_section = line.bars[j].draw_begin_repeat;
                    bool prev_ended_section = line.bars[j - 1].draw_end_repeat;
                    can_extend = !starts_new_section && !prev_ended_section;
                }

                if (can_extend)
                {
                    line.volta_spans.back().last_bar_index = j;
                }
                else
                {
                    volta_span vsn;
                    vsn.first_bar_index = j;
                    vsn.last_bar_index  = j;
                    vsn.numbers         = vs;
                    line.volta_spans.push_back(vsn);
                }
            }
            // Closed-flag pass: a span is closed iff its last bar has
            // draw_end_repeat set.  Done as a second pass so the
            // grouping logic above stays focused on adjacency.
            for (auto& vsn : line.volta_spans)
                vsn.closed = line.bars[vsn.last_bar_index].draw_end_repeat;
        }

        line.draw_section_end_rule = ends_section[line_idx];
        // line.rect covers the volta zone + bar row so hit-tests and
        // insertion-slot anchors see the full visual height.
        line.rect = QRectF(content_rect.left(), y,
                          content_rect.width(),
                          volta_zone_h + actual_bar_h);
        lines_.push_back(std::move(line));

        qreal spacing = ends_section[line_idx] ? k_line_spacing : k_line_spacing_normal;
        y += volta_zone_h + actual_bar_h + spacing;
    }

    // Insertion slots: a same_line slot per line (computed inside
    // compute_insertion_slots from each line's own geometry), plus a
    // single next_line slot below the song's last line.  We pass in
    // the next_line slot's parameters here — its top y (one
    // k_line_spacing_normal below the last line's bottom, matching
    // what a freshly laid-out next line would use) and its bar height
    // (the last line's bar-row height, ie. excluding any volta zone
    // above the bars so the ghost reads as "an empty bar like the
    // others" rather than "an empty bar plus a phantom volta gap").
    const auto& last_line = lines_.back();
    qreal last_bar_h = last_line.rect.height()
                       - (last_line.has_voltas ? k_volta_zone_height : 0.0);
    compute_insertion_slots(content_rect,
                            bars_left,
                            /*last_line_bottom=*/last_line.rect.bottom()
                                                  + k_line_spacing_normal,
                            /*last_line_bar_h=*/last_bar_h);

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
// Generates the dashed-ghost hover affordances for new-bar insertion.
// Three flavours:
//   * Empty song            -> one "first_bar" slot at content top-left.
//   * Every line            -> a "same_line" slot at the right edge of
//                              that line, allowing the line to be
//                              extended by one bar.  Extension growing
//                              past bars_per_line is allowed by design —
//                              the mouse gesture is the explicit way to
//                              author an extended line.
//   * Last line of the song -> additionally, a "next_line" slot below
//                              the line, starting a fresh row.  Middle
//                              lines don't need this because the next
//                              line already exists in the layout.
//
// Slot widths are deliberately suggestive rather than precise; the real
// bar width is computed from chord contents after the user types into the
// editor.  Using the host line's last bar width (or a fixed default when
// empty) produces a visual hint that lines up naturally with that line.
//
// Vertical anchoring: each same_line slot uses its line's last bar's
// rect.top() and the line's bar-row height (which excludes the volta
// zone above the bars).  This keeps the ghost aligned with the bars
// even on volta-bearing lines — the bracket lives in the zone above,
// the slot lives in the bar row.
void song_body_widget::compute_insertion_slots(const QRectF& content_rect,
                                               qreal bars_left,
                                               qreal last_line_bottom,
                                               qreal last_line_bar_h)
{
    constexpr qreal k_default_slot_w = 80.0;

    if (song_.empty())
    {
        insertion_slot s;
        s.kind      = insertion_slot_kind::first_bar;
        s.rect      = QRectF(content_rect.left(), content_rect.top(),
                             k_default_slot_w, last_line_bar_h);
        s.insert_at = 0;
        insertion_slots_.push_back(s);
        return;
    }

    // Per-line same_line slots.  Walk every line, accumulating a flat
    // song-bar index so each slot knows where its commit should insert
    // the new bar.
    std::size_t song_idx_cursor = 0;
    for (std::size_t li = 0; li < lines_.size(); ++li)
    {
        const auto& line = lines_[li];
        if (line.bars.empty())
        {
            // Defensive: a line with no bars produces no extension
            // affordance.  This shouldn't happen under the layout
            // invariants but staying tolerant keeps the renderer
            // crash-free if it ever does.
            continue;
        }

        const auto& last_bar = line.bars.back();
        // Bar-row height: exclude the volta zone if this line has one.
        // The slot represents an empty bar that would join this line,
        // and an empty bar doesn't carry a phantom volta gap.
        const qreal bar_row_h = line.rect.height()
                              - (line.has_voltas ? k_volta_zone_height : 0.0);

        insertion_slot s;
        s.kind      = insertion_slot_kind::same_line;
        s.rect      = QRectF(last_bar.rect.right() + k_inter_bar_spacing,
                             last_bar.rect.top(),
                             last_bar.rect.width(),
                             bar_row_h);
        // Insert position: one past this line's last bar in the flat
        // song vector.
        s.insert_at = song_idx_cursor + line.bars.size();
        insertion_slots_.push_back(s);

        song_idx_cursor += line.bars.size();
    }

    // next_line slot — only for the song's last line.  Anchored at the
    // bar-column left edge, on a fresh row below the last line.  Width
    // matches the last line's same_line slot so the two ghosts look
    // like siblings to the eye when both are visible (e.g. moving the
    // mouse from one to the other).
    const auto& last_line = lines_.back();
    qreal slot_w = last_line.bars.empty()
                   ? k_default_slot_w
                   : last_line.bars.back().rect.width();
    insertion_slot ns;
    ns.kind      = insertion_slot_kind::next_line;
    ns.rect      = QRectF(bars_left, last_line_bottom, slot_w, last_line_bar_h);
    ns.insert_at = song_.bars().size();
    insertion_slots_.push_back(ns);
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

    QFont title_font("Georgia", 12, QFont::Bold);
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
    // flicker.  Also skipped while an annotation tool is active: the
    // user is placing annotations, and a dashed bar-insertion ghost
    // would compete visually with the rubber-band / anchor dots.
    const bool tool_active = annotation_layer_.tool_active();
    if (hovered_slot_ >= 0
        && hovered_slot_ < static_cast<int>(insertion_slots_.size())
        && !active_editor_
        && !dragging_divider_
        && !tool_active)
    {
        paint_insertion_slot(painter, insertion_slots_[hovered_slot_]);
    }

    // Empty section gutter hover: draw a dashed outline matching the
    // would-be label box so the user can see the click target.  Lines
    // that already carry a section label use their painted box as the
    // affordance — no extra outline.  Same suppression when a tool is
    // active.
    if (hovered_empty_section_line_ >= 0
        && hovered_empty_section_line_ < static_cast<int>(lines_.size())
        && !active_editor_
        && !dragging_divider_
        && !tool_active)
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

    // Annotation overlay sits on top of bar chrome but below the inline
    // editor (which Qt paints as a child widget, automatically on top).
    // Two passes:
    //   * paint() draws committed annotations — text boxes first, then
    //     connectors above them so an arrow that lands on a text box's
    //     border has its tip visible at the boundary.
    //   * paint_overlay() draws live drag visuals: the rubber-band line
    //     or box, anchor dots on text boxes / line edges (only during
    //     an endpoint drag), and the hovered-anchor highlight.
    annotation_layer_.paint(painter);
    annotation_layer_.paint_overlay(painter);
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
    // The vertical rule runs the full height of the widget, including through
    // the title row, so the margin column reads as a continuous left strip.
    painter.drawLine(QPointF(margin_width_, 0), QPointF(margin_width_, height()));
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
                            line.has_articulation,
                            bl.draw_begin_repeat, bl.draw_end_repeat);

        if (bl.show_continuation_dot)
            paint_continuation_dot(painter, bl.rect, bl.num_center_y);

        ++bar_idx;
    }

    // Volta brackets sit in the zone reserved above the bars by
    // compute_layout.  Painted after the bars so the bracket's leftmost
    // edge cleanly overrides any antialiasing fringe from the bar
    // glyphs below it.
    if (line.has_voltas)
        paint_volta_brackets(painter, line);

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
// paint_volta_brackets
// ---------------------------------------------------------------------------
// One labelled horizontal bracket per volta_span on the line.  The
// bracket sits in the volta zone (height k_volta_zone_height) reserved
// above the bar row by compute_layout.  Geometry per bracket:
//
//   top_y ─── ┌────────── 1., 2. ──────────────┐ ─── top_y
//             │                                │
//             │ left hook (always present)     │ right hook (only if
//             │                                │  span.closed — i.e.,
//             │                                │  this volta ends with
//             │                                │  a repeat back, not
//             │                                │  fall-through)
//   bottom_y─ ┘                                └ ─── bottom_y
//             │                                │
//             ▼ bar's left edge                ▼ bar's right edge
//
// The horizontal line tracks the top of the bracket; the hooks
// descend from there down to the BASELINE of the volta label — so
// the label digit ("1.", "2.", etc.) visually "sits on" the bottom
// edge of each hook.  The hook's vertical extent is therefore
// determined by the label font's ascent, not by any chord-row
// geometry; the entire bracket lives in the volta zone above the
// bars.
//
// "Open" final volta: the right hook is omitted but the horizontal
// line still extends to the same x — visually the bracket trails off
// over the music, matching the convention readers expect.
void song_body_widget::paint_volta_brackets(QPainter& painter,
                                            const line_layout& line) const
{
    if (line.volta_spans.empty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen bracket_pen(Qt::black, 1.2);
    painter.setPen(bracket_pen);
    painter.setBrush(Qt::NoBrush);

    // Volta-zone vertical bounds: the zone occupies the top
    // k_volta_zone_height of the line rect.  Bracket line sits a few
    // pixels below the top edge so the label has room above the
    // descender of glyphs like "1.".
    qreal zone_top    = line.rect.top();
    qreal bracket_y   = zone_top + 1.0;

    // Font for the volta label — a small bold rendering so a 1., 2.,
    // 3. reads as a label rather than as part of the chord row.
    QFont label_font = fonts_.modifier;
    label_font.setBold(false);
    QFontMetricsF label_fm(label_font);
    painter.setFont(label_font);

    // Hooks descend to the BASELINE of the volta label glyphs ("1.",
    // "2.", etc.).  This visually anchors each hook to the label that
    // sits inside the bracket — the hook and the label digit feel
    // like they belong to the same shape, rather than the hook being
    // a short disconnected stub above the chord row.  Note: this is
    // the LABEL's baseline, not the chord number's — the bracket lives
    // entirely in the volta zone, separate from the chord row below.
    // label_fm.ascent() + 1.0 mirrors how the label itself is placed
    // below; the hook ends exactly where the label's digits "sit."
    qreal hook_bot_y = bracket_y + label_fm.ascent() + 1.0;

    for (const auto& vs : line.volta_spans)
    {
        // Bracket horizontal extent: from the first bar's left edge to
        // the last bar's right edge.  These bars include their repeat-
        // mark slots, so the bracket naturally encloses any begin/end
        // repeat that sits at the boundary.
        if (vs.first_bar_index >= line.bars.size()
            || vs.last_bar_index  >= line.bars.size())
            continue;

        const auto& first_bl = line.bars[vs.first_bar_index];
        const auto& last_bl  = line.bars[vs.last_bar_index];
        qreal left_x  = first_bl.rect.left();
        qreal right_x = last_bl.rect.right();

        // Build the label string from the numbers set.  std::set is
        // ordered, so iteration produces "1., 2." rather than "2., 1.".
        // The model indexes voltas from 0 internally but they're shown
        // to readers as 1-based, so we +1 each number for display.
        QString label;
        bool first = true;
        for (unsigned n : vs.numbers)
        {
            if (!first) label += ", ";
            label += QString::number(n + 1) + ".";
            first = false;
        }

        // Horizontal top stroke.
        painter.drawLine(QPointF(left_x, bracket_y),
                         QPointF(right_x, bracket_y));

        // Left hook — always present.  Reaches down to the baseline
        // of the volta label, visually connecting the bracket's
        // vertical stroke to the digit that names this ending.
        painter.drawLine(QPointF(left_x, bracket_y),
                         QPointF(left_x, hook_bot_y));

        // Right hook — only on closed brackets.  An open bracket lets
        // the eye glide rightward into the following music, signalling
        // "this is the final pass."
        if (vs.closed)
        {
            painter.drawLine(QPointF(right_x, bracket_y),
                             QPointF(right_x, hook_bot_y));
        }

        // Label.  Inset from the left hook by a small gap so it's not
        // jammed against the hook stroke.  The label's baseline
        // coincides with hook_bot_y by construction — both derive from
        // bracket_y + label_fm.ascent() + 1.0 — so the digit visually
        // "sits on" the hook's bottom edge.
        constexpr qreal k_label_x_inset = 4.0;
        painter.drawText(QPointF(left_x + k_label_x_inset, hook_bot_y),
                         label);
    }

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

    // Annotation layer gets first crack at the click.  It claims:
    //   * any click while a non-none tool is active (creating new
    //     annotations or initiating drags on empty canvas);
    //   * clicks that land on an existing annotation, even in bar mode,
    //     so the user can interact with annotations they previously
    //     placed without first switching tools (matches Google
    //     Drawings — you can grab a line you drew earlier regardless
    //     of which tool's selected).
    // When the layer takes a selection on an annotation we also clear
    // the bar selection so the two systems don't both highlight at
    // once.
    if (annotation_layer_.mouse_press(event->pos(),
                                      event->button(),
                                      event->modifiers()))
    {
        if (annotation_layer_.has_selection())
            clear_selection();
        update();
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

    // Annotation double-click (e.g. on a text box → open its inline
    // editor) gets first refusal.  We test this before the bar
    // double-click path because text boxes can overlay bars, and the
    // user's intent on double-clicking a visible text box is to edit
    // its text, not the bar underneath.
    if (annotation_layer_.mouse_double_click(p))
    {
        update();
        return;
    }

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

    // If the annotation layer has a drag in flight, it consumes every
    // move and we repaint to advance the rubber-band.  Otherwise, if a
    // tool is active, the layer still gets to dictate the cursor
    // (crosshair for empty canvas, resize cursor over a handle, etc.)
    // and we bypass the bar-hover affordances.
    if (annotation_layer_.mouse_move(event->pos()))
    {
        update();
        return;
    }
    if (annotation_layer_.tool_active())
    {
        setCursor(annotation_layer_.cursor_for(event->pos()));
        bool changed = false;
        if (hovered_slot_ != -1)               { hovered_slot_ = -1;               changed = true; }
        if (hovered_empty_section_line_ != -1) { hovered_empty_section_line_ = -1; changed = true; }
        if (changed) update();
        return;
    }
    // Even in bar mode, a hover over an existing annotation should show
    // a move / resize cursor.  cursor_for returns ArrowCursor for empty
    // canvas in bar mode, so this is a cheap short-circuit.
    Qt::CursorShape ann_cur = annotation_layer_.cursor_for(event->pos());
    if (ann_cur != Qt::ArrowCursor)
    {
        setCursor(ann_cur);
        // Don't return here — we still want the rest of the hover
        // logic to clear any bar-side outlines that may be lingering.
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
    // Pull pending repaint flag from annotation_layer: when the cursor
    // crosses a text-box boundary in any tool mode, that box's hover
    // border needs to appear or disappear.  mouse_move tracks the
    // transition and sets a flag; we union it with the bar-side
    // `changed` so a single update() covers both.
    if (annotation_layer_.take_hover_repaint())
        changed = true;
    if (changed) update();

    setCursor(new_slot_hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void song_body_widget::mouseReleaseEvent(QMouseEvent* event)
{
    // The annotation layer claims the release iff it had a drag in
    // flight; on release it commits the new/moved annotation and
    // repaints.  Tested first so that a connector drop is treated as
    // ending an annotation gesture, not as a stray click that would
    // also tickle the divider state.
    if (annotation_layer_.mouse_release(event->pos()))
    {
        update();
        return;
    }
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
    // Clear bar-side hover affordances...
    bool changed = false;
    if (hovered_slot_ != -1)               { hovered_slot_ = -1;               changed = true; }
    if (hovered_empty_section_line_ != -1) { hovered_empty_section_line_ = -1; changed = true; }
    // ...and the annotation-side idle-hover position too, so anchor
    // dots near the last cursor location don't keep painting after the
    // cursor leaves.  Don't condition this on `tool_active()`: the
    // layer's mouse_leave() is a cheap reset and clearing
    // unconditionally is robust to mid-leave tool changes.
    annotation_layer_.mouse_leave();
    update();
    (void)changed;  // update() above already covers both cases
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

    // Annotation layer gets first crack at Esc / Del / Backspace.  Its
    // key_press returns false if it doesn't consume the key (e.g. Del
    // with no annotation selected), letting the existing bar-side
    // shortcuts fire normally.  For Esc, the layer's precedence chain
    // is: cancel in-flight drag → clear annotation selection → exit
    // annotation tool → fall through.  If we get a "true" back, the
    // layer handled the key and we just repaint.
    if (annotation_layer_.key_press(event->key(), event->modifiers()))
    {
        update();
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
// Opens an inline editor over the hovered ghost rectangle.  Slot choice
// is the source of truth about layout: the kind decides *what kind of
// edit* (start the song, extend a line, start a new line below the
// last), and the slot's insert_at tells the commit handler *where* in
// the flat bars vector the new bar lands.
//
// Commit is atomic: either the new bar is inserted *and* the surrounding
// is_eol flags update consistently, or neither happens.  If
// bar::parse_user_input throws on the chord text, every write performed
// during this commit is rolled back so the chart returns to exactly its
// pre-click state.
//
// Empty input reverts — we never want to insert a chordless bar just
// because the user clicked and pressed Enter.
//
// The slot's kind and insert_at are captured by value (not its index),
// because the layout — and therefore the insertion_slots_ vector — is
// rebuilt on every model change.  What the user picked is durable; the
// vector index is an implementation detail of the current frame.
//
// same_line semantics across the slot's two flavours:
//   * Mid-line slot (insert_at points to a non-end-of-song position):
//     the bar at insert_at - 1 was the line's tail (is_eol == true).
//     On commit we transfer that flag to the new bar — the new bar
//     becomes the line's new tail, and the previous tail becomes an
//     interior bar of the same line.  Result: the line grows by one,
//     and the bar that *follows* the line in song order still starts
//     a fresh line as it did before.
//   * Last-line slot (insert_at == bars.size()): the bar at insert_at -
//     1 is the song's last bar.  Same transfer applies — whatever its
//     is_eol bit was, it moves to the new bar.  Visually the bit's
//     value is irrelevant on a song-tail bar (there's nothing after
//     it), but keeping a single rule for both flavours keeps the
//     logic uniform and side-effect-free.
//
// next_line: the new bar starts a fresh line below the last.  We set
// is_eol on the previous last bar so that's where the break lives.  The
// new bar's own is_eol stays false (it's the song's new last bar).
//
// first_bar: empty song, no neighbours to touch.  Append and done.
void song_body_widget::edit_new_bar(std::size_t slot_index)
{
    if (slot_index >= insertion_slots_.size())
        return;

    const insertion_slot& slot = insertion_slots_[slot_index];
    insertion_slot_kind kind = slot.kind;
    const std::size_t insert_at = slot.insert_at;

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
        [this, kind, insert_at](const QString& text) -> bool
        {
            QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
                return false;  // revert: don't insert an empty bar

            // Stage the entire edit on a copy of the bars vector so a
            // parser exception can be cleanly aborted.  Only after the
            // new bar parses successfully do we publish the copy to the
            // model.  This keeps the operation atomic.
            std::vector<model::bar> bars = song_.bars();

            // Parse the new bar first; if it throws, `bars` is
            // untouched and we return false without publishing.
            model::bar new_bar;
            try
            {
                new_bar.parse_user_input(trimmed.toStdString());
            }
            catch (const std::exception&)
            {
                return false;  // chart returns to exact pre-click state
            }

            // Apply per-kind is_eol bookkeeping, then insert.  Clamp
            // insert_at defensively in case the model changed under us
            // (shouldn't happen under the single-editor invariant).
            const std::size_t pos = std::min(insert_at, bars.size());

            if (kind == insertion_slot_kind::first_bar)
            {
                // Empty song — nothing to fix up.
            }
            else if (kind == insertion_slot_kind::same_line)
            {
                // Extend a line.  The bar immediately before pos is the
                // line's old tail.  Transfer its is_eol to the new bar:
                // the new bar becomes the tail, the old tail becomes an
                // interior bar of the same line.  This rule is identical
                // for mid-line and last-line same_line slots.
                if (pos > 0)
                {
                    bool was_eol = bars[pos - 1].is_eol();
                    bars[pos - 1].is_eol(false);
                    new_bar.is_eol(was_eol);
                }
            }
            else  // next_line
            {
                // Start a new line below the last.  Mark the previous
                // last bar as a line-ender; the new bar's own is_eol
                // stays false (it's the song's new last bar).
                if (!bars.empty())
                    bars.back().is_eol(true);
            }

            bars.insert(bars.begin() + pos, std::move(new_bar));
            song_.bars(bars);
            rebuild();
            return true;
        },
        /*placeholder=*/tr("e.g. 1 4 5"));

    // Mark this as a bar-editing session so Tab chains.  The "index"
    // we track is where the bar will land after commit — i.e., the
    // slot's insert_at.  After commit, Tab-advance computes prev + 1
    // and either extends or, when we're past the song's end, kicks
    // back to extend_with_tab for another append.
    editing_bar_index_ = insert_at;
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

    // Pick the desired kind first, then find the matching slot whose
    // insert_at points to the song's end.  There are now multiple
    // same_line slots — one per line — so kind alone is no longer
    // unique; Tab always means "append at the song's end", which
    // restricts us to slots with insert_at == bars.size().
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

    const std::size_t song_end = song_.bars().size();
    for (std::size_t i = 0; i < insertion_slots_.size(); ++i)
    {
        const auto& s = insertion_slots_[i];
        if (s.kind == desired && s.insert_at == song_end)
        {
            edit_new_bar(i);
            return;
        }
    }
    // No matching slot — fall back to the first slot of the desired
    // kind regardless of insert_at, then to slot 0.  Under current
    // compute_insertion_slots logic the song-end branch above always
    // hits (the desired kind is always present at the song end), but
    // the fallback keeps a future layout change from silently
    // dropping the Tab.
    for (std::size_t i = 0; i < insertion_slots_.size(); ++i)
    {
        if (insertion_slots_[i].kind == desired)
        {
            edit_new_bar(i);
            return;
        }
    }
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
// edit_text_box — inline editor for an annotation text box's text
// ---------------------------------------------------------------------------
// Called by the annotation_layer through the edit-text callback we wired
// in the constructor.  Uses the multi-line editor (not the single-line
// one) because annotations should support Enter for newlines and only
// commit on focus-out, matching Google Drawings text boxes.  Mirrors
// edit_bar's pattern otherwise: capture the id, look it up freshly on
// commit so we don't carry a pointer across a model mutation, and let
// close_line_editor's revert-on-failure handle bad input by returning
// false.  In practice every input is valid here — a blank annotation
// isn't useful but isn't an error either, so we always return true.
void song_body_widget::edit_text_box(std::uint64_t text_box_id,
                                     const QRectF& widget_rect)
{
    auto* tb = song_.annotes().find_text_box(text_box_id);
    if (!tb)
        return;
    const QString initial = tb->text;

    open_multiline_editor(
        widget_rect,
        initial,
        text_box_id,
        [this, text_box_id](const QString& text) -> bool
        {
            if (auto* t = song_.annotes().find_text_box(text_box_id))
            {
                t->text = text;
                update();
                return true;
            }
            // Annotation vanished mid-edit (e.g. another path deleted
            // it).  Treat as a no-op revert.
            return false;
        });
}

// ---------------------------------------------------------------------------
// Inline-editor plumbing
// ---------------------------------------------------------------------------

// Force the standard "looks like printed paper" palette onto an editor
// widget: white background, black text, even when the desktop is in
// dark mode.  The chart's own painting already uses Qt::white as the
// background everywhere (see paintEvent and paint_to_rect), but Qt
// widgets like QLineEdit and QPlainTextEdit inherit their palette
// from the application's theme — so on a dark-themed desktop you'd
// get a dark editor sitting on a white chart, which looks broken.
//
// We touch four palette roles:
//   * Base       — the editor's background fill
//   * Text       — typed text color
//   * Window     — frame/chrome background (matters for the
//                  QPlainTextEdit's frame between the document area
//                  and the widget border)
//   * WindowText — chrome foreground
// PlaceholderText is left to its default derivation from Text, which
// gives a faded-black placeholder against the white base — same look
// the user would see on a light-theme desktop.
//
// This goes through palette rather than stylesheet because mixing
// stylesheets with palette overrides interacts unpredictably in Qt6
// (stylesheets win for most properties but not all), and palette is
// the documented "I want specific colors regardless of theme" knob.
static void apply_print_palette(QWidget* w)
{
    QPalette pal = w->palette();
    pal.setColor(QPalette::Base,       Qt::white);
    pal.setColor(QPalette::Text,       Qt::black);
    pal.setColor(QPalette::Window,     Qt::white);
    pal.setColor(QPalette::WindowText, Qt::black);
    // Selected-text colors for the editor's own selection (when the
    // user drags to highlight some characters they typed).  Use a
    // light blue background with black text — readable on both light
    // and dark desktops because we picked them ourselves.  Without
    // setting these explicitly, the editor would pick up the system's
    // highlight color, which on dark themes is typically a low-
    // contrast color that disappears against the white background we
    // just forced.
    pal.setColor(QPalette::Highlight,        QColor(180, 213, 254));
    pal.setColor(QPalette::HighlightedText,  Qt::black);
    w->setPalette(pal);
}

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
    apply_print_palette(edit);
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

// ---------------------------------------------------------------------------
// open_multiline_editor — for annotation text boxes
// ---------------------------------------------------------------------------
// Same overall shape as open_line_editor but with a QPlainTextEdit
// inside.  Three notable differences:
//   * Enter inserts a newline (default QPlainTextEdit behavior), so we
//     don't need to override key handling for it.  Esc still cancels —
//     that lives in eventFilter, which already dispatches by `watched
//     == active_editor_` and doesn't care about widget type.
//   * Focus loss commits.  QPlainTextEdit has no editingFinished
//     signal — we install a focus-out hook via the event filter
//     (FocusOut case below in eventFilter).
//   * As the user types, the editor's documentLayout reports the
//     content's natural size.  We resize the editor vertically to fit
//     and write the new height back to the text box, so the box grows
//     to fit content live.  Width stays at the original rect's width;
//     wrapping inside the box happens via QPlainTextEdit's word-wrap
//     mode, matching the same wrap rule the painter uses.
void song_body_widget::open_multiline_editor(
    const QRectF& rect,
    const QString& initial,
    std::uint64_t text_box_id,
    std::function<bool(const QString&)> commit)
{
    if (active_editor_)
        close_line_editor(/*commit_value=*/false);

    auto* edit = new QPlainTextEdit(this);
    apply_print_palette(edit);
    edit->setPlainText(initial);
    // Word-wrap at the editor's width.  The painter uses WordWrap too,
    // so what the user sees while editing matches what they'll see
    // when the editor closes (modulo the editor's own frame chrome).
    edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    edit->setWordWrapMode(QTextOption::WordWrap);
    // No scrollbars — the box grows instead.  If a user paints
    // themselves into a corner with a tiny box and a huge novel,
    // they'll still get scrollbars from QPlainTextEdit's default
    // behavior, but the steady-state UX is "type, box grows."
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setFrameStyle(QFrame::Panel | QFrame::Plain);
    // Match the text-box paint font so the visual replacement is
    // seamless.  annotation_layer holds the font for paint, but the
    // editor needs its own copy — set it from our chord-number font
    // family at the same 75% size used in paint_text_box.
    QFont tb_font = fonts_.number;
    tb_font.setPointSize(std::max(8, int(tb_font.pointSize() * 0.75)));
    edit->setFont(tb_font);

    edit->setGeometry(rect.toRect());
    edit->show();
    edit->setFocus(Qt::MouseFocusReason);
    edit->selectAll();
    edit->installEventFilter(this);

    active_editor_       = edit;
    editor_commit_       = std::move(commit);
    editing_text_box_id_ = text_box_id;

    // Grow-to-fit: every time the document changes, ask the document
    // layout for its natural size and resize both the editor and the
    // underlying text box's rect height to match.  Width is unchanged
    // so horizontal growth doesn't happen automatically (Google's
    // text boxes grow vertically when you type; horizontal growth
    // requires a manual resize).  We clamp to the original rect's
    // height as a minimum so a user shrinking their text doesn't
    // shrink the box past where they originally drew it.
    const qreal min_h = rect.height();
    const qreal w     = rect.width();
    connect(edit, &QPlainTextEdit::textChanged, this,
        [this, edit, min_h, w, text_box_id]() {
            // Natural document height = layout's reported size + the
            // editor's frame thickness (top+bottom).  Cap at the
            // remaining height of the widget so a runaway box doesn't
            // overflow.
            QSizeF doc_size = edit->document()->size();
            const int frame = edit->frameWidth() * 2;
            qreal needed = doc_size.height() + frame
                         + edit->contentsMargins().top()
                         + edit->contentsMargins().bottom();
            if (needed < min_h) needed = min_h;
            const QPoint top_left = edit->geometry().topLeft();
            const qreal max_h = std::max(min_h,
                qreal(height()) - top_left.y() - 2);
            if (needed > max_h) needed = max_h;
            // Apply to the editor.  Width is unchanged.
            edit->setFixedHeight(int(needed));
            edit->setFixedWidth(int(w));
            // Mirror into the model so the text box on commit
            // (and during the live edit, any neighboring repaint)
            // sees the new height.  We don't repaint the chart while
            // editing because the editor occludes the box — but
            // mutating the model now means the rebuild after close
            // already has the right rect.
            if (auto* tb = song_.annotes().find_text_box(text_box_id))
            {
                if (tb->rect.height() != needed)
                {
                    tb->rect.setHeight(needed);
                }
            }
        });
}

bool song_body_widget::close_line_editor(bool commit_value)
{
    if (!active_editor_)
        return false;

    QWidget* edit = active_editor_;
    auto commit = std::move(editor_commit_);

    // Read text in a type-dispatched way: single-line editors store
    // their text in QLineEdit::text(), multi-line in
    // QPlainTextEdit::toPlainText().  Trimming policy is the same for
    // both — bar/section/etc. editors all trim, and text-box
    // annotations should trim too (a box with trailing whitespace and
    // no visible content would be a UX foot-gun).
    QString text;
    if (auto* le = qobject_cast<QLineEdit*>(edit))
        text = le->text().trimmed();
    else if (auto* pe = qobject_cast<QPlainTextEdit*>(edit))
        text = pe->toPlainText().trimmed();

    // Tear down before invoking the commit callback so the callback's
    // rebuild() can repaint without the (about-to-be-deleted) editor on
    // top, and so any focus-loss noise during teardown can't re-enter us.
    active_editor_ = nullptr;
    // Drop the bar-edit marker too — Tab-advance only makes sense while
    // a bar editor is open, and we're about to either commit it, revert
    // it, or both.  Any follow-up open_line_editor (e.g. from the Tab-
    // advance path itself) will re-set this if appropriate.
    editing_bar_index_.reset();
    editing_text_box_id_.reset();
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
    // FocusOut commits the multi-line editor.  QPlainTextEdit has no
    // editingFinished signal, and we want the same focus-out-commits
    // behavior QLineEdit has (which QLineEdit gives us for free via
    // editingFinished).  We *only* honor focus-out for the multi-line
    // editor — single-line editors already auto-commit through
    // editingFinished, and double-handling here would race.  The
    // discriminator is editing_text_box_id_: set iff the active editor
    // is the multi-line one.
    if (watched == active_editor_ &&
        event->type() == QEvent::FocusOut &&
        editing_text_box_id_.has_value())
    {
        // Commit on focus loss.  Match the existing QLineEdit
        // editingFinished behavior: pretend the user pressed Enter on
        // a single-line editor.  This also handles "click anywhere
        // outside the editor" because that's the same chain
        // mousePressEvent already uses to clear the editor (and the
        // click delivers a FocusOut to the editor before our handler
        // runs).
        close_line_editor(/*commit_value=*/true);
        // Don't return true here: the focus event still needs to
        // propagate to whatever the user clicked on, so they're not
        // left in a dead state.  Returning false lets Qt deliver the
        // event to subsequent filters and the target widget.
        return false;
    }

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
            // it's the slot's insert_at — the index the new bar will
            // occupy after commit.  For end-of-song slots that equals
            // bars.size() at editor-open time; for mid-line slots it's
            // somewhere inside the existing vector.  In either case,
            // after a successful insert the new bar is at exactly
            // this index in the post-commit vector.
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
                // bars.size().  A new-bar commit grows bars.size() by
                // one: if the bar landed mid-song (mid-line slot),
                // prev + 1 lands inside the existing vector; if it
                // landed at the song's end (last-line slot or
                // next_line slot), prev + 1 == bars.size().  In all
                // cases the same condition correctly distinguishes
                // "edit existing next bar" from "extend the song
                // again with a fresh slot."
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

// ---------------------------------------------------------------------------
// Selection-driven bar attribute edits
// ---------------------------------------------------------------------------
// Mutate every bar in selected_bars_ through the standard "copy → mutate →
// publish" pattern used by edit_bar / edit_section.  Doing the bulk apply
// on a single copy keeps each call to song_.bars() (which itself copies
// the vector internally) to exactly one, and keeps the model atomically
// consistent: if a future setter ever throws, none of the selected bars
// are left half-modified in song_'s storage.
//
// Bounds-check each index against the current bars vector so a stale
// selection (which shouldn't happen under the single-editor invariant
// but could if the surface area grows) can't index past the end.
void song_body_widget::apply_repeat_to_selection(model::bar::repeat_status st)
{
    if (selected_bars_.empty())
        return;

    std::vector<model::bar> bars_copy = song_.bars();
    bool any_changed = false;
    for (std::size_t idx : selected_bars_)
    {
        if (idx >= bars_copy.size())
            continue;
        if (bars_copy[idx].repeat() == st)
            continue;
        bars_copy[idx].repeat(st);
        any_changed = true;
    }
    if (!any_changed)
        return;
    song_.bars(bars_copy);
    rebuild();
}

void song_body_widget::apply_voltas_to_selection(const std::set<unsigned>& voltas)
{
    if (selected_bars_.empty())
        return;

    std::vector<model::bar> bars_copy = song_.bars();
    bool any_changed = false;
    for (std::size_t idx : selected_bars_)
    {
        if (idx >= bars_copy.size())
            continue;
        if (bars_copy[idx].voltas() == voltas)
            continue;
        bars_copy[idx].clear_voltas();
        for (unsigned v : voltas)
            bars_copy[idx].add_volta(v);
        any_changed = true;
    }
    if (!any_changed)
        return;
    song_.bars(bars_copy);
    rebuild();
}

// ---------------------------------------------------------------------------
// apply_end_line_to_selection
// ---------------------------------------------------------------------------
// Sets is_eol = true on every selected bar.  Same copy-mutate-publish
// pattern as the other selection-driven setters.  Idempotent: if every
// selected bar already ends a line, the early-return on any_changed
// skips the publish/rebuild cycle entirely.
//
// Applying this to the song's last bar is a harmless no-op visually —
// there's no following line for a break to introduce — but the flag
// is still set in the model, matching the existing behaviour of
// parse_user_input and other code paths that touch is_eol uniformly
// regardless of bar position.
void song_body_widget::apply_end_line_to_selection()
{
    if (selected_bars_.empty())
        return;

    std::vector<model::bar> bars_copy = song_.bars();
    bool any_changed = false;
    for (std::size_t idx : selected_bars_)
    {
        if (idx >= bars_copy.size())
            continue;
        if (bars_copy[idx].is_eol())
            continue;
        bars_copy[idx].is_eol(true);
        any_changed = true;
    }
    if (!any_changed)
        return;
    song_.bars(bars_copy);
    rebuild();
}

std::optional<model::bar::repeat_status>
song_body_widget::common_repeat_of_selection() const
{
    if (selected_bars_.empty())
        return std::nullopt;

    const auto& bars = song_.bars();
    std::optional<model::bar::repeat_status> shared;
    for (std::size_t idx : selected_bars_)
    {
        if (idx >= bars.size())
            continue;
        if (!shared)
            shared = bars[idx].repeat();
        else if (*shared != bars[idx].repeat())
            return std::nullopt;
    }
    return shared;
}

std::optional<std::set<unsigned>>
song_body_widget::common_voltas_of_selection() const
{
    if (selected_bars_.empty())
        return std::nullopt;

    const auto& bars = song_.bars();
    std::optional<std::set<unsigned>> shared;
    for (std::size_t idx : selected_bars_)
    {
        if (idx >= bars.size())
            continue;
        if (!shared)
            shared = bars[idx].voltas();
        else if (*shared != bars[idx].voltas())
            return std::nullopt;
    }
    return shared;
}

// ---------------------------------------------------------------------------
// insert_bar_relative_to_selection
// ---------------------------------------------------------------------------
// Inserts a new (empty) bar adjacent to the current selection.  The bar
// is added to the model immediately — *not* gated on a follow-up commit
// in an inline editor — so the user sees the new column land in the
// layout as soon as the menu item is invoked.  This matches the
// expectation that a "Insert 1 before/after" menu action behaves like a
// structural-change command (akin to Delete or End line) rather than an
// edit-affordance opener: the action either succeeds visibly, or
// nothing changes.  The new bar is left selected so a follow-up click
// (or any Bar-menu action) acts on it without an extra step; clicking
// it opens the inline editor via the existing double-click path.  An
// empty bar carries a small reserved chord-slot width
// (bar_renderer::k_empty_bar_chord_slot_w) so it reads as a recognisable
// column even before chords are entered.
//
// Anchor position:
//   * "before" → at the lowest selected index N.  After insertion the
//     new bar takes position N and everything from the old N onward
//     shifts up by one.
//   * "after"  → one past the highest selected index M.  The new bar
//     lands at position M+1.  If M was the song's last bar, the new
//     bar is simply appended.
//
// Line-break preservation: menu-driven insertion respects the song's
// preferred bars_per_line, but only on lines that aren't already
// extended past it.  An "extended line" is one whose original count
// (before this insertion) was already > bars_per_line — those were
// authored deliberately past the cap (e.g. via the mouse-driven
// extension affordance), and inserting onto such a line should honour
// that intent and extend the extension further, not split it.  When
// the line *wasn't* extended (original count <= bpl) and the insertion
// would push it over the cap, we set is_eol on whichever bar is now
// the bars_per_line'th from the start of that line, pushing the rest
// onto a new line.  Inserting after a bar that already ended a line
// (or after the song's last bar) lands the new bar at the start of
// the next line, and that line's count is checked separately.  No
// line *other* than the one the new bar lands on is touched, so
// unrelated existing extensions elsewhere in the song are preserved.
void song_body_widget::insert_bar_relative_to_selection(bool after)
{
    if (selected_bars_.empty())
        return;

    // Anchor bar = first selected (for "before") or last selected (for
    // "after").  selected_bars_ is a std::set so begin/rbegin give those
    // in O(1).
    const std::size_t anchor_song_idx = after ? *selected_bars_.rbegin()
                                              : *selected_bars_.begin();
    const std::size_t insert_at       = after ? anchor_song_idx + 1
                                              : anchor_song_idx;

    if (anchor_song_idx >= song_.bars().size())
        return;   // stale selection; defensive

    // If an inline editor happens to be open (defensive — menu actions
    // typically take focus away from any editor first), commit before
    // we mutate the model out from under it.
    if (active_editor_)
        close_line_editor(/*commit_value=*/true);

    std::vector<model::bar> bars = song_.bars();
    const std::size_t pos = std::min(insert_at, bars.size());

    // The new bar is a default-constructed empty bar — no chords, no
    // section, no voltas, is_eol == false.  The renderer handles
    // chord-less bars (see bar_renderer::width_hint / paint), giving
    // them a reserved chord-slot width so they're visible in the
    // layout while waiting for the user's first edit.
    bars.insert(bars.begin() + pos, model::bar{});

    // Enforce the bars_per_line cap on the line that received the new
    // bar — but only if that line wasn't already extended past the
    // cap.  An already-extended line was authored deliberately, and
    // the user's intent on a further insert is to extend it further,
    // not split it.
    //
    // Locate the owner line in the new vector: walk forward from index
    // 0 tracking line-starts, and pick the line whose [first, last]
    // inclusive range contains `pos`.  Then count its bars.  The
    // original (pre-insertion) count is line_count - 1 because exactly
    // one of the bars in the owner line is the one we just inserted.
    // If orig_count <= bpl and line_count > bpl, set is_eol on the bar
    // at first + bpl - 1.  The old line-tail (which had is_eol=true to
    // terminate the line in the first place, unless it was the song's
    // last bar) keeps its flag — pushing it to the next line where it
    // continues to end that next line.
    //
    // Special case: if the new bar lands on a brand-new line (because
    // its predecessor had is_eol=true, or pos==0 and the predecessor
    // doesn't exist), the owner line starts at pos.  Same algorithm
    // handles this uniformly — we scan for the first line-start <= pos.
    const unsigned bpl = song_.bars_per_line();
    if (bpl > 0 && pos < bars.size())
    {
        std::size_t first = 0;
        for (std::size_t i = 0; i < pos; ++i)
        {
            if (bars[i].is_eol())
                first = i + 1;
        }
        // Walk forward from `first` to find the line's end (the bar
        // with is_eol, or the song's last bar).
        std::size_t last = bars.size() - 1;
        for (std::size_t i = first; i < bars.size(); ++i)
        {
            if (bars[i].is_eol())
            {
                last = i;
                break;
            }
        }
        const std::size_t line_count = last - first + 1;
        const std::size_t orig_count = line_count - 1;
        if (orig_count <= bpl && line_count > bpl)
        {
            // Bar at position (first + bpl - 1) becomes the new
            // line-tail.  If that index equals `last`, we were going
            // to set is_eol on the bar that already has it — harmless
            // no-op.  Otherwise this splits the line, with the
            // original line-tail moving to a new next line where it
            // still ends that line (its own is_eol is unchanged).
            const std::size_t new_tail = first + bpl - 1;
            bars[new_tail].is_eol(true);
        }
    }

    song_.bars(bars);
    rebuild();

    // Leave the newly inserted bar selected so follow-up Bar-menu
    // actions (e.g. Repeat or Voltas...) act on it without requiring
    // an extra click, and so the selection background visibly marks
    // where the bar landed.  Deliberately *do not* open an inline
    // editor here: the user invoked a structural-change menu item, not
    // an edit affordance, and forcing them into edit mode would steal
    // focus from the menu they were just using.  Clicking (or
    // double-clicking) the new bar opens the editor via the existing
    // mouse path.
    select_bar_only(pos);
}

// ---------------------------------------------------------------------------
// prompt_voltas_for_selection
// ---------------------------------------------------------------------------
// Modal prompt for a comma-separated list of 1-indexed volta numbers.
// Empty input is a deliberate "clear all voltas" gesture — symmetric
// with edit_section's empty-clears-the-label behaviour.  Non-numeric
// or zero tokens silently abort the apply: the user already had a
// chance to fix typos in the dialog, and re-prompting would be noisy.
//
// Each accepted number is decremented before storage to match the
// model's 0-indexed convention (see bar.hpp's voltas_ comment).
void song_body_widget::prompt_voltas_for_selection()
{
    if (!has_selection())
        return;

    // Prefill from the selection iff every selected bar carries the
    // same volta set.  Mixed selections leave the field empty — there
    // is no single right answer to show, and prefilling one bar's
    // values would silently overwrite the others on accept.
    QString initial;
    if (auto shared = common_voltas_of_selection())
    {
        QStringList parts;
        for (unsigned v : *shared)
            parts << QString::number(v + 1);   // 0-indexed → 1-indexed
        initial = parts.join(", ");
    }

    bool ok = false;
    QString text = QInputDialog::getText(
        this, tr("Voltas"),
        tr("Volta numbers (comma-separated, 1-indexed; empty to clear):"),
        QLineEdit::Normal, initial, &ok);
    if (!ok)
        return;   // user cancelled — leave the model untouched

    std::set<unsigned> parsed;
    const QStringList tokens = text.split(',', Qt::SkipEmptyParts);
    for (const QString& tok : tokens)
    {
        QString t = tok.trimmed();
        if (t.isEmpty())
            continue;   // tolerate "1, ,2" — common typo
        bool num_ok = false;
        unsigned n = t.toUInt(&num_ok);
        if (!num_ok || n == 0)
            return;     // invalid token: silently abort the whole apply
        parsed.insert(n - 1);  // store 0-indexed
    }

    apply_voltas_to_selection(parsed);
}

// ---------------------------------------------------------------------------
// show_bar_context_menu
// ---------------------------------------------------------------------------
// Builds the Bar context menu fresh each call so the checked-states on
// the Repeat submenu always reflect the current selection.  Sharing a
// long-lived QMenu would force us to either re-sync those states before
// every popup or accept stale checks — building fresh is simpler and
// the menu is tiny.
//
// The Repeat items are mutually exclusive via a QActionGroup.  When the
// selection is homogeneous, exactly one is checked; when it's mixed,
// none are — clicking any item still applies that choice to every
// selected bar, collapsing the mix.
void song_body_widget::show_bar_context_menu(const QPoint& global_pos)
{
    using repeat_status = model::bar::repeat_status;

    QMenu menu(this);

    // Insertion items come first, separated from the attribute-edit
    // items (Repeat / Voltas).  The separator is the standard visual
    // cue that the two groups are distinct kinds of action: structural
    // changes to the song above, attribute tweaks below.  "End line"
    // sits with the insertion group because it, too, changes the
    // song's structural shape — it forces a line break at the selected
    // bar(s) rather than tweaking an attribute.
    QAction* insert_before_act = menu.addAction(tr("Insert 1 before"));
    connect(insert_before_act, &QAction::triggered, this, [this]() {
        insert_bar_relative_to_selection(/*after=*/false);
    });
    QAction* insert_after_act = menu.addAction(tr("Insert 1 after"));
    connect(insert_after_act, &QAction::triggered, this, [this]() {
        insert_bar_relative_to_selection(/*after=*/true);
    });
    QAction* end_line_act = menu.addAction(tr("End line"));
    connect(end_line_act, &QAction::triggered, this, [this]() {
        apply_end_line_to_selection();
    });
    // Delete sits with the other structural-change items.  Mirrors the
    // shortcut text on the menubar copy so users learn the keystroke
    // from either path.  Qt only displays a single sequence in menu
    // text; we pick Delete as the canonical one and leave Backspace as
    // the unadvertised-but-functional alternate (also bound on the
    // menubar action — both paths reach the same slot).
    QAction* delete_act = menu.addAction(tr("Delete"));
    delete_act->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(delete_act, &QAction::triggered, this, [this]() {
        apply_delete_to_selection();
    });
    menu.addSeparator();

    QMenu* repeat_menu = menu.addMenu(tr("Repeat"));
    auto* repeat_group = new QActionGroup(&menu);
    repeat_group->setExclusive(true);

    struct entry { const char* label; repeat_status value; };
    static const entry entries[] = {
        { "None",  repeat_status::NONE  },
        { "Begin", repeat_status::BEGIN },
        { "End",   repeat_status::END   },
    };

    const auto shared = common_repeat_of_selection();
    for (const auto& e : entries)
    {
        QAction* a = repeat_menu->addAction(tr(e.label));
        a->setCheckable(true);
        a->setActionGroup(repeat_group);
        a->setChecked(shared.has_value() && *shared == e.value);
        repeat_status v = e.value;
        connect(a, &QAction::triggered, this, [this, v]() {
            apply_repeat_to_selection(v);
        });
    }

    QAction* voltas_act = menu.addAction(tr("Voltas..."));
    connect(voltas_act, &QAction::triggered, this, [this]() {
        prompt_voltas_for_selection();
    });

    menu.exec(global_pos);
}

// ---------------------------------------------------------------------------
// contextMenuEvent — right-click on a bar
// ---------------------------------------------------------------------------
// Right-clicking a bar that's already in the selection leaves the
// selection alone (so users can right-click a multi-bar selection to
// edit them all).  Right-clicking outside the current selection replaces
// the selection with just the clicked bar — the file-manager idiom.
// Right-clicking on chart whitespace does nothing: there's no bar to
// act on, and silently popping up a menu with no target would be
// confusing.
void song_body_widget::contextMenuEvent(QContextMenuEvent* event)
{
    // If a left-click inline editor is open, commit it before showing
    // the menu — mirrors the behaviour of left-clicking elsewhere in
    // the chart while editing.
    if (active_editor_)
        close_line_editor(/*commit_value=*/true);

    const QPointF p = event->pos();
    int bar_idx = hit_test_bar(p);
    if (bar_idx < 0)
    {
        event->ignore();
        return;
    }

    auto idx = static_cast<std::size_t>(bar_idx);
    if (selected_bars_.find(idx) == selected_bars_.end())
        select_bar_only(idx);

    show_bar_context_menu(event->globalPos());
    event->accept();
}

} // namespace nashville::view
