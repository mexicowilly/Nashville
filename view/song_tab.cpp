#include "song_tab.hpp"
#include "song_info_panel.hpp"
#include "../database.hpp"
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QEvent>

namespace nashville::view
{

song_tab::song_tab(std::unique_ptr<model::song> song,
                   std::optional<song_id> id,
                   database& db,
                   QWidget* parent)
    : QWidget(parent)
    , song_(std::move(song))
    , id_(id)
    , db_(db)
{
    // Baseline for change detection.  Taken before the song_widget is
    // built so the snapshot predates the view installing its annotation
    // anchor-resolver — the copy is pure content, no captured callback.
    // A freshly-loaded or freshly-created song therefore reads as
    // "unchanged" until the user actually edits something, so opening a
    // song never spuriously advances its modification time.
    last_saved_ = std::make_unique<model::song>(*song_);

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // Content area: a stack of [chart, info page].  Chart is index 0 so the
    // tab always opens showing the chart.
    stack_ = new QStackedWidget(this);
    vl->addWidget(stack_);

    // song_widget takes a reference to the song; we own the song
    // outright via unique_ptr, so handing it &*song_ is stable for
    // the tab's lifetime.  The widget is parented to `this`, so Qt's
    // parent-deletion cascade takes care of teardown order before
    // song_ is destroyed.
    widget_ = new song_widget(*song_, this);
    stack_->addWidget(widget_);          // index 0

    // The metadata Info page.  It mutates song_->meta() in place on Save and
    // tells us via committed(); we respond by kicking the debounced save,
    // which persists the change and advances modification_time (the save
    // path's same_content_as already covers every metadata field).  Its back
    // control returns to the chart.
    info_panel_ = new song_info_panel(*song_, this);
    stack_->addWidget(info_panel_);      // index 1
    connect(info_panel_, &song_info_panel::committed, this, [this]() {
        save_timer_.start();
    });
    connect(info_panel_, &song_info_panel::closed, this, [this]() {
        show_chart();
    });

    // Save debounce.  Single-shot so we don't re-fire while the user
    // is mid-burst; each paintEvent restarts the timer in
    // eventFilter, so the save runs once activity has settled.
    save_timer_.setSingleShot(true);
    save_timer_.setInterval(k_save_debounce_ms);
    connect(&save_timer_, &QTimer::timeout, this, &song_tab::save);

    // Install the event filter on the song_widget AND its descendants
    // we care about — the body widget is where most paintEvents
    // happen.  Qt's eventFilter cascade only delivers events targeted
    // at `watched`, so installing on every paint-emitting widget is
    // necessary.  Easiest robust approach: install on the song_widget
    // and recursively on its children.  In practice the song_widget
    // has a known fixed child tree (a QScrollArea wrapping a
    // song_body_widget), so we install on the body specifically.
    widget_->installEventFilter(this);
    if (auto* body = widget_->body())
    {
        body->installEventFilter(this);

        // A title change is the one edit the debounced, content-blind save
        // path can't handle on its own: flush it now so the database row is
        // renamed by id immediately, then tell the host so it can update the
        // tab label and the song list.
        connect(body, &song_body_widget::title_changed, this, [this]() {
            flush_save();
            emit renamed();
        });

        // A confirmed "delete all bars" is the one edit allowed to persist an
        // empty song past the autosave's clobber guard.  Authorise exactly
        // that next write, then flush it immediately so the intent isn't left
        // hanging on the debounce.
        connect(body, &song_body_widget::song_emptied, this, [this]() {
            allow_empty_persist_once_ = true;
            flush_save();
        });
    }
}

song_tab::~song_tab()
{
    // If a save is pending, flush before destruction.  Without this,
    // closing the tab via the QTabWidget close button could lose
    // up to one second of edits.  flush_save is idempotent so the
    // common path (host called flush_save → then deleted us)
    // doesn't re-write.
    flush_save();
}

QString song_tab::tab_name() const
{
    return QString::fromStdString(song_->name());
}

void song_tab::show_info()
{
    // Always (re)enter the Info page in read-only view mode showing fresh
    // model data, so it never opens mid-edit from a previous visit.
    if (info_panel_)
        info_panel_->show_view_mode();
    if (stack_)
        stack_->setCurrentWidget(info_panel_);
}

void song_tab::show_chart()
{
    if (stack_)
        stack_->setCurrentWidget(widget_);
}

bool song_tab::eventFilter(QObject* /*watched*/, QEvent* event)
{
    // Restart the debounce on every paint.  paintEvent fires after
    // any UI-driven model mutation (the song_body_widget's update()
    // calls), so this catches edits without needing to instrument
    // every mutator.  False negatives are impossible (every mutation
    // triggers a repaint), false positives (no-op repaints from
    // hover state, focus, etc.) just restart the timer on a
    // no-content-change — a redundant save is cheap.
    //
    // We do NOT consume the event — returning false lets it continue
    // to the target widget as normal.
    if (event->type() == QEvent::Paint)
    {
        // QTimer::start() restarts the count, so consecutive paint
        // events keep pushing the save out until activity stops.
        save_timer_.start();
    }
    return false;
}

void song_tab::flush_save()
{
    // If a save is queued, kill the timer and write now.  If no save
    // is queued, still call save() — flush_save is called on tab close,
    // and the user expects "close this tab" to mean "persist".  save()
    // now no-ops when nothing has changed since the last write (see the
    // #1 guard there), so an immediate close with no edits is cheap and
    // won't rewrite an identical song.
    //
    // Suppression check: when the host is tearing down the app, the
    // database may be gone or about to go.  Don't try to write.
    save_timer_.stop();
    if (save_suppressed_)
        return;
    save();
}

void song_tab::save()
{
    // Suppression also short-circuits direct timer-driven saves, in
    // case a debounced save happens to fire during teardown after
    // the host has called suppress_destructor_save.
    if (save_suppressed_)
        return;
    // Saves are keyed by row identity, not by name.  The first save for a
    // never-persisted song inserts and records the id; every save after
    // updates that row by id.  Because the row is located by id, a title
    // change is written as an ordinary column update — there is no rename
    // path and no way to orphan the old row.

    // Whole-song content diff against the last persisted snapshot.  The
    // autosave is content-blind — any repaint can trigger it, including
    // selection and hover — so this diff is what tells a real edit apart from
    // a no-op repaint.  Comparing whole-song content (rather than
    // instrumenting mutators) also captures edits that never pass through a
    // song setter, such as the in-place custom-beats change and every
    // annotation edit: if the diff can see it, it counts.  creation_time and
    // modification_time are excluded from the comparison.
    const bool content_changed =
        !last_saved_ || !song_->same_content_as(*last_saved_);

    // (#1) Skip writes that would change nothing.  Without this an identical
    // song was rewritten every idle second (and, worse, that rewrite deletes
    // and re-inserts every bar each time).  A never-persisted song (no id_
    // yet) is the one exception: it must be inserted at least once so its row
    // exists, even if it hasn't been edited since it was created.
    if (id_ && !content_changed)
        return;

    // (#2) Clobber guard.  Never let an autosave replace an already-persisted,
    // non-empty set of bars with an empty one.  The write path rebuilds the
    // bar rows from song_->bars() (delete-all, then re-insert), so a stray or
    // ill-timed save that ran while that vector was momentarily empty would
    // wipe the chart down to just its title and margin.  last_saved_ mirrors
    // what the row currently holds, so it is the right reference.
    //
    // The exception is a deliberate "delete all bars", which the user has
    // explicitly confirmed (song_body_widget::song_emptied set the one-shot
    // flag below): that empty write is allowed through exactly once.  Every
    // other empty-over-non-empty write is refused.
    const bool would_wipe_bars =
        song_->bars().empty() && last_saved_ && !last_saved_->bars().empty();
    if (would_wipe_bars)
    {
        if (!allow_empty_persist_once_)
            return;
        allow_empty_persist_once_ = false;   // one-shot: consume the OK
    }

    // Advance modification_time only when content actually changed, so a mere
    // click never bumps the timestamp.
    if (content_changed)
    {
        using namespace std::chrono;
        const auto now = time_point_cast<milliseconds>(system_clock::now());
        auto& mt = song_->meta().modification_time;
        // Only ever move the modification time forward.  The wall clock can
        // step backward (NTP correction, a manual clock change), and a song
        // may have been last saved on a machine whose clock runs ahead of
        // this one; in either case stamping a raw "now" could move the
        // timestamp backward.  Advancing to at least the previous value plus
        // one millisecond keeps it strictly monotonic across edits, while
        // still tracking real time whenever the clock is sane.  The exact
        // instant is secondary; never going backward is the contract.
        const auto floor = mt + milliseconds(1);
        mt = (now > floor) ? now : floor;
    }

    try
    {
        if (id_)
            // would_wipe_bars is only true here when the user explicitly
            // confirmed a "delete all bars" (the one-shot flag above let it
            // past the guard).  Authorise the empty write in exactly that
            // case; otherwise the database layer's own backstop refuses it.
            db_.update_song(*id_, *song_, /*allow_empty_bars=*/would_wipe_bars);
        else
            id_ = db_.insert_song(*song_);
        // Refresh the baseline only after a successful write, so a failed
        // save leaves last_saved_ reflecting what's really in the database
        // and the next attempt re-evaluates (and, if needed, re-stamps).
        last_saved_ = std::make_unique<model::song>(*song_);

        // Keep the Info page's read-only "Modified" line current.  Cheap and
        // harmless when the chart is showing or the panel is mid-edit (it
        // only rebuilds the hidden view page).
        if (info_panel_)
            info_panel_->refresh_view();
    }
    catch (const std::exception& e)
    {
        // Swallow but log: a save failure shouldn't crash the editor,
        // and the user is mid-session — we can't pop a dialog every
        // second.  The loggable base on database carries an spdlog
        // logger but we don't have access to it from here; rely on
        // the database layer's own logging.  If save failures become a
        // real problem we can add a status-bar indicator.
        (void)e;
    }
}

} // namespace nashville::view
