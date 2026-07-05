#pragma once

#include "../db_ids.hpp"
#include <QWidget>
#include <memory>
#include <optional>
#include <string>

class QTabWidget;
class QToolButton;

namespace nashville
{
class database;
namespace model { class song; }
}

namespace nashville::view
{

class side_panel;
class song_tab;
class modal_overlay;

// The central widget for the application.  Composes:
//   * a hamburger QToolButton in the upper-left that toggles the
//     side panel's expanded state
//   * the side panel (collapsed by default)
//   * a QTabWidget of open song tabs (the main editing area)
//
// Layout is a QHBoxLayout: [side_panel] [main column].  The main
// column is a QVBoxLayout: [hamburger row] [tab widget].  The
// hamburger sits in its own row above the tabs rather than next to
// them because the hamburger needs to be the leftmost thing on
// screen even when the side panel is collapsed — putting it inside
// the tab area would mean it moves around when tabs are added.
//
// The main_window does NOT own the database.  The database lives in
// `app`, which passes it by reference here.  We hold the reference
// so we can populate the side panel's lists on construction and on
// refresh.
class main_window : public QWidget
{
    Q_OBJECT
public:
    main_window(database& db, QWidget* parent = nullptr);

    // Open a song by name.  If a tab for it already exists, switch
    // to that tab.  Otherwise load from the database and add a new
    // tab.  Silently no-ops if the name isn't in the database (the
    // caller's UI usually only offers names from the songs list, so
    // this is mostly defensive).
    void open_song(const std::string& name);

    // Open a tab for every song the database recorded as open when it was
    // last closed.  Called when a database becomes the live workspace: at
    // startup (by app, once the UI is wired) and after switching files.  A
    // no-op when nothing was recorded.  Safe to call on a clean slate only —
    // it assumes no tabs are currently open.
    void restore_open_tabs();

    // Record the set of currently-open songs into the live database so they
    // can be reopened next time it's opened.  Called when the database is
    // about to close: on app quit (after flushing) and before switching
    // files.  Songs without a row id yet (never saved) are skipped.
    void persist_open_tabs();

    // Force a save on every open tab.  Called by app on quit so no
    // in-flight edits are lost.  Each tab's flush_save handles its
    // own debounce-timer state.
    void flush_all_tabs();

    // Close (and delete) every open tab without flushing — the
    // assumption is that flush_all_tabs has already run.  Used by
    // app's destructor to tear down tabs explicitly BEFORE the
    // database is destroyed; otherwise Qt's parent-deletion cascade
    // runs each tab's destructor after db_ is gone, and the
    // destructor's defensive flush_save hits a dead reference.
    void close_all_tabs_without_saving();

    // Repopulate the side panel's lists from the database.  Called
    // on app start, after closing a tab (in case the close added /
    // removed something at the DB level), and when the side panel
    // is shown (in case external state changed).
    void refresh_lists();

    // Apply a new chart font scale to every currently-open tab and
    // relayout each.  Called by app when the user picks a size from the
    // View > Text size menu.  Tabs opened afterwards read the persisted
    // scale themselves on construction, so only open charts need this.
    void apply_font_scale_to_all_tabs(qreal scale);

    // Currently focused tab's song_tab, or nullptr if no tabs are
    // open.  Used by app::wire_*_menu so menu actions target the
    // visible tab's song widget.
    song_tab* current_tab() const;

    // Side-panel selection accessors.  Used by the Playlist menu's
    // Delete action, which targets whatever's selected in the side
    // panel (playlists don't have tabs).  Returns an empty string
    // when nothing is selected.
    std::string selected_song_name() const;
    std::string selected_playlist_name() const;

    // Menu-driven entry points.  app's menu wiring calls these for
    // the Song / Playlist menus' New and Delete items.  They share
    // their implementation with the side-panel's "+" and right-click
    // paths, so the user gets identical prompts and confirmations
    // regardless of how they got here.
    void prompt_new_song();
    void prompt_new_playlist();
    void confirm_delete_song(const std::string& name);
    void confirm_delete_playlist(const std::string& name);

    // Save entry points, wired to the File menu.
    //   save()    — Ctrl+S.  While the database is in memory (untitled) this
    //               is the same as save_as(), since there's no path yet.  Once
    //               file-backed it just flushes every open tab to make the
    //               continuous auto-save durable this instant (the file is
    //               already live, so there's nothing else to do).
    //   save_as() — always prompts for a path, flushes every open tab so the
    //               last few seconds of edits are included, then moves the
    //               database to that file via database::move_to_file().  On
    //               failure the database layer guarantees we're left on the
    //               intact in-memory database with no file damaged, so the
    //               handler just reports the error and returns.  Returns true
    //               iff the database was actually written to a file.
    //   prompt_open() — open a different file as the workspace.  If the
    //               current session is unsaved scratch, first offers to save
    //               it; then asks for a file and switches the live database to
    //               it (database::open_file), closing the old tabs.
    void save();
    bool save_as();
    void prompt_open();

signals:
    // Emitted when the current tab changes or when a tab opens /
    // closes — basically any state change that affects what
    // current_tab() returns.  The app uses this to re-wire menu
    // actions to the new visible song's widgets.
    void current_tab_changed(song_tab* tab);

protected:
    // Installed by app as an event filter on the top-level QMainWindow
    // (main_window doesn't own that window — app does — so this is the
    // one place main_window reaches outside its own widget tree).
    // Intercepts QEvent::Close to guard against the same data-loss gap
    // save_as()/prompt_open() already guard against: a fresh session's
    // database lives entirely in memory (see database::database()'s
    // default ":memory:" backing) until the user explicitly saves, so
    // quitting without ever doing that silently discards everything
    // with no warning. Mirrors prompt_open()'s existing Save/Discard
    // framing rather than introducing a new three-way Save/Discard/
    // Cancel pattern modal_overlay doesn't support: ignore the close,
    // ask, and either re-issue a close that goes through once resolved
    // (Save or Discard) or do nothing (Esc / click outside — same
    // "changed my mind" semantics modal_overlay::confirm already gives
    // every other Yes/No prompt in this app).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Tab close handler.  Flushes the closing tab, drops it from
    // the QTabWidget, refreshes the side panel (since save may
    // have updated it).  Wired to QTabWidget::tabCloseRequested.
    void close_tab_at(int index);

    // Create a tab around an already-loaded song (and its optional row id),
    // wire its close button and rename handler, append it, and focus it.
    // Shared by open_song (load-by-name) and restore_open_tabs (bulk reopen),
    // so both produce identically-wired tabs.
    void add_tab(std::unique_ptr<model::song> song, std::optional<song_id> id);

    // Find the index of the tab editing `name`, or -1 if none.
    // Linear scan — number of open tabs is small (a handful at
    // most), so hashing is overkill.
    int find_tab_by_name(const std::string& name) const;

    // True iff the live database is still the default in-memory
    // session (see database::database()'s ":memory:" default) AND it
    // has something in it worth not silently discarding. An empty
    // in-memory session has nothing at risk; a file-backed one is
    // already durable. Shared by prompt_open() (before replacing the
    // workspace) and eventFilter() (before quitting) so both apply the
    // exact same rule for "is there unsaved scratch work right now."
    bool has_unsaved_scratch() const;

    // Reflect the current database file (or "Untitled" when in memory) in
    // the top-level window title.  Called after a successful save.
    void update_window_title();

    // Show the Open dialog and, if a file is chosen, switch the live database
    // to it and rebuild the UI around the new file.  The caller (prompt_open)
    // has already dealt with saving/discarding the current workspace.
    void open_replacing_current();

    database&     db_;
    QToolButton*  hamburger_  = nullptr;
    side_panel*   panel_      = nullptr;
    QTabWidget*   tabs_       = nullptr;
    // In-widget modal overlay for prompts and confirmations.
    // Used instead of QDialog because Wayland (and any other
    // protocol where the application can't control window
    // placement) renders QDialog positioning unreliable.  See
    // modal_overlay.hpp for the full rationale.
    modal_overlay* overlay_   = nullptr;

    // Set once the user has resolved the quit-confirmation prompt
    // (Saved, or explicitly chose Discard). eventFilter re-issues a
    // close() on the watched window to let it actually go through;
    // this flag is what stops that second close from re-triggering
    // the same prompt.
    bool quit_confirmed_ = false;
};

} // namespace nashville::view
