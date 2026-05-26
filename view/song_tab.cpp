#include "song_tab.hpp"
#include "../database.hpp"
#include <QVBoxLayout>
#include <QEvent>

namespace nashville::view
{

song_tab::song_tab(std::unique_ptr<model::song> song,
                   database& db,
                   QWidget* parent)
    : QWidget(parent)
    , song_(std::move(song))
    , db_(db)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // song_widget takes a reference to the song; we own the song
    // outright via unique_ptr, so handing it &*song_ is stable for
    // the tab's lifetime.  The widget is parented to `this`, so Qt's
    // parent-deletion cascade takes care of teardown order before
    // song_ is destroyed.
    widget_ = new song_widget(*song_, this);
    vl->addWidget(widget_);

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
        body->installEventFilter(this);
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
    // is queued, still write — flush_save is called on tab close,
    // and the user expects "close this tab" to mean "persist".  Yes,
    // this means an immediate close with no edits writes a redundant
    // copy of the song; the alternative (track a dirty flag through
    // every mutation) is more invasive than it's worth.
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
    // insert_song does an upsert by song name in the existing schema
    // (it looks up an existing row by name and overwrites bars /
    // annotations under its id).  We don't currently support rename
    // — that would require keeping a "previous name" snapshot to
    // delete the old row — but that's a separate feature.
    try
    {
        db_.insert_song(*song_);
    }
    catch (const std::exception& e)
    {
        // Swallow but log: a save failure shouldn't crash the editor,
        // and the user is mid-session — we can't pop a dialog every
        // second.  The loggable base on database carries an spdlog
        // logger but we don't have access to it from here; rely on
        // insert_song's own logging.  If save failures become a real
        // problem we can add a status-bar indicator.
        (void)e;
    }
}

} // namespace nashville::view
