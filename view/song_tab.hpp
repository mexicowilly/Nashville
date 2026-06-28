#pragma once

#include "../model/song.hpp"
#include "../db_ids.hpp"
#include "song_widget.hpp"
#include <QWidget>
#include <QTimer>
#include <memory>
#include <optional>

namespace nashville
{
class database;
}

namespace nashville::view
{

// One open editing context: owns the in-memory song, hosts the
// editable song_widget, and runs a debounced auto-save back to the
// database.
//
// Lifecycle
//   * Constructed with an rvalue model::song (moved in) and a
//     reference to the application's single database.  The tab takes
//     ownership of the song — closing the tab is the only way the
//     song's memory gets freed.
//   * Edits in the song_widget mutate song_ in place.  The widget's
//     own repaint pipeline is the cue: every paintEvent restarts the
//     debounce timer.  This catches every UI-driven mutation without
//     having to instrument the mutators themselves, at the cost of
//     also restarting the timer on no-op repaints (hover, focus
//     changes).  Redundant writes are cheap compared to the
//     invasiveness of explicit dirty-tracking.
//   * When the timer fires (~1 sec idle), save() runs: the first save
//     inserts the row and records its id; every save after updates by
//     that id.  Tab still alive after save; user keeps editing.
//   * close() (called by the tab widget's close button, by app on
//     quit, or by force_save_and_close from the host) forces an
//     immediate save and then drops the song.  Idempotent.
//
// The host (main_window / app) is responsible for placing this widget
// in a QTabWidget — song_tab is just a content widget, not the tab
// chrome itself.
class song_tab : public QWidget
{
    Q_OBJECT
public:
    // Take the song by value so the caller can decide whether to
    // pass an rvalue (most common) or a copy (testing).  Storing as
    // a std::unique_ptr because model::song isn't trivially movable
    // — it carries an annotations_ field with a std::function
    // resolver that captures `this`, and moving that around would
    // dangle the captures.  Heap allocation + stable address fixes
    // it.
    song_tab(std::unique_ptr<model::song> song,
             std::optional<song_id> id,
             database& db,
             QWidget* parent = nullptr);
    ~song_tab() override;

    // The name shown in the QTabWidget tab.  Reads through to
    // song_->name() so a future rename feature would be a one-liner
    // (mutate the model, ask the host to refresh tab text).
    QString tab_name() const;

    // Identity for "is this song already open?" checks.  The host
    // consults this when opening a song to avoid loading the same
    // name twice.
    const std::string& song_name() const { return song_->name(); }

    // Force a synchronous write to the database, regardless of timer
    // state.  Used on tab-close, app-quit, and anywhere else the
    // host needs to be certain the current state is persisted before
    // proceeding.
    void flush_save();

    // Suppress the destructor's defensive flush_save call.  Called
    // during teardown when the database is about to be destroyed
    // (or has been destroyed): without this, the tab's destructor
    // would attempt a save through a dead reference.  Once
    // suppressed, subsequent calls to flush_save() are also no-ops
    // for the same reason.  One-way switch — there's no resurrection.
    void suppress_destructor_save() { save_suppressed_ = true; }

    // Access to the underlying song for callers that need to inspect
    // it (e.g. the annotation menu wiring in app, which calls into
    // the body widget's tool mode).  Pointer is stable for the
    // tab's lifetime.
    model::song&       song()       { return *song_; }
    const model::song& song() const { return *song_; }

    // Access to the song_widget for the same reasons.  The annotation
    // menu wiring needs body() to set the active tool.
    song_widget* widget() const { return widget_; }

signals:
    // Emitted after the user renames the song in the chart.  By the time
    // this fires the new name has been flushed to the database (so the row
    // is renamed by id, not duplicated), so the host can safely refresh the
    // tab label and the side-panel song list.
    void renamed();

protected:
    // Capture every paint event from the song_widget (and its child
    // song_body_widget) so we can restart the debounce timer.  Using
    // an event filter rather than subclassing means we don't have to
    // touch song_widget's class hierarchy.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void save();   // hits the database

    std::unique_ptr<model::song> song_;
    // Row identity for this song, once it has one.  Empty only for a song
    // that has never been saved; the first save() inserts and records the
    // id, and every save after that updates by id.  Because saves are keyed
    // by id, a title change is just a normal column in the update — no
    // rename call, and no chance of orphaning the old row.
    std::optional<song_id>       id_;
    database&                    db_;
    song_widget*                 widget_ = nullptr;
    QTimer                       save_timer_;
    // Set when the host is tearing down the application and the
    // database is going away.  Once set, both the debounce timer
    // callback and flush_save() short-circuit to avoid hitting a
    // dead reference.  See main_window::close_all_tabs_without_saving
    // for the use case.
    bool                         save_suppressed_ = false;

    // Save debounce: how long after the last edit we wait before
    // writing.  1 second is the standard auto-save cadence; short
    // enough that a crash loses at most a second of work, long
    // enough that a user typing into a text box doesn't trigger
    // a save on every keystroke.
    static constexpr int k_save_debounce_ms = 1000;
};

} // namespace nashville::view
