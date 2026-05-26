#pragma once

#include <QWidget>
#include <vector>
#include <string>

class QListWidget;
class QPropertyAnimation;
class QToolButton;

namespace nashville::view
{

// Slide-out panel showing the database's song and playlist names.
// Sits to the left of the tab widget in the main window.  Two list
// widgets stacked vertically: songs on top, playlists below.  Names
// come from database::select_song_names() / select_playlist_names(),
// pushed in via refresh().  Double-clicking a song name emits
// song_double_clicked(name); the application is expected to either
// focus an existing tab for that song or load it from the database
// into a new tab.  Double-clicking a playlist name is currently a
// no-op — we still emit the signal in case the host wants to do
// something with it later.
//
// The panel can be collapsed to width 0 (hidden) or expanded to its
// natural width (showing).  Animation lives in show() / hide() —
// QPropertyAnimation on maximumWidth.  Width settles at fixed values
// so layout doesn't keep adjusting during the animation.
class side_panel : public QWidget
{
    Q_OBJECT
public:
    explicit side_panel(QWidget* parent = nullptr);

    // Replace the contents of the songs list with `names`.  Order
    // preserved.  Currently selected name (if still present in the
    // new list) is re-selected.  Idempotent; safe to call from
    // refresh hooks after open/close.
    void set_song_names(const std::vector<std::string>& names);

    // Same for playlists.
    void set_playlist_names(const std::vector<std::string>& names);

    // Animation control.  These can be called repeatedly with the
    // same target state — they're no-ops if we're already there
    // (or already animating toward there).  Width animates between
    // 0 (collapsed) and k_expanded_width (shown).
    void show_animated();
    void hide_animated();
    void toggle_animated();

    bool is_expanded() const { return expanded_; }

    // Currently-selected name in each list, or empty string if no
    // selection.  main_window uses these to know what the menu-bar
    // Delete actions should target — the side-panel selection is
    // the natural cursor for delete operations.  Strings are copies,
    // safe to keep across mutations of the panel.
    QString selected_song_name() const;
    QString selected_playlist_name() const;

    // The width the panel takes when fully expanded.  Exposed so the
    // host can calculate layout margins or know how far to offset
    // sibling widgets if it ever cares to.
    static constexpr int k_expanded_width = 240;

signals:
    // Emitted on double-click of a song / playlist name.  The string
    // is the displayed name (which is also the unique key in the
    // database — names are unique per song and per playlist).
    void song_double_clicked(const QString& name);
    void playlist_double_clicked(const QString& name);

    // Emitted when the user clicks the "+" button next to the songs
    // or playlists section header.  The host (main_window) handles
    // the actual creation: prompt for a name, validate, insert into
    // the database, refresh the list.  Keeping the side panel
    // ignorant of the database means it's testable in isolation and
    // can be reused in any future shell that wants the same
    // affordance.
    void add_song_requested();
    void add_playlist_requested();

    // Emitted when the user picks "Delete" from a row's right-click
    // context menu.  Carries the name that was right-clicked.  The
    // host handles confirmation and the actual database delete; the
    // side panel just emits the user's intent.
    void delete_song_requested(const QString& name);
    void delete_playlist_requested(const QString& name);

protected:
    // Reposition the floating "+" buttons when their list widgets are
    // resized.  Each list has its own button as a child; the filter
    // watches both lists and moves the appropriate button on the
    // QEvent::Resize for each.  We use an event filter rather than
    // subclassing QListWidget for the two lists, because the change
    // is purely cosmetic and a one-line subclass per list would be
    // worse for readers of the code than a five-line filter on the
    // owning panel.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild_list(QListWidget* list, const std::vector<std::string>& names);

    QListWidget* songs_     = nullptr;
    QListWidget* playlists_ = nullptr;
    // Floating "+" buttons, parented to the corresponding list
    // widgets (NOT to the side_panel).  Kept as side_panel members
    // so the eventFilter can find them by which list raised the
    // resize event.
    QToolButton* songs_add_btn_     = nullptr;
    QToolButton* playlists_add_btn_ = nullptr;
    QPropertyAnimation* anim_ = nullptr;
    bool expanded_ = false;
};

} // namespace nashville::view
