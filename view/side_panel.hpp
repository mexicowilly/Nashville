#pragma once

#include <QWidget>
#include <vector>
#include <string>
#include <utility>

class QListWidget;
class QTreeWidget;
class QPropertyAnimation;
class QToolButton;
class QComboBox;

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
    // The animated/resized width.  Writing it pins the panel to an exact width
    // (min == max), which is what lets the panel grow past its content's
    // natural size when the user drags it wider.  The collapse/expand
    // animation drives this property between 0 and the expanded width.
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth)
public:
    explicit side_panel(QWidget* parent = nullptr);

    int  panelWidth() const { return maximumWidth(); }
    void setPanelWidth(int w) { setMinimumWidth(w); setMaximumWidth(w); }

    // Replace the contents of the songs list with `names`.  Order
    // preserved.  Currently selected name (if still present in the
    // new list) is re-selected.  Idempotent; safe to call from
    // refresh hooks after open/close.
    void set_song_names(const std::vector<std::string>& names);

    // Replace the songs list with name + secondary-value rows.  The secondary
    // value (shown right-aligned, dimmed) is whatever the current sort key
    // resolves to for each song — e.g. the album when sorting by album.  An
    // empty secondary (as when sorting by name) shows just the name.
    void set_song_rows(
        const std::vector<std::pair<std::string, std::string>>& rows);

    // Same for playlists.
    void set_playlist_names(const std::vector<std::string>& names);

    // Select the sort-key dropdown entry whose token matches `token`, without
    // emitting sort_key_changed (this reflects state the host loaded from the
    // database, it isn't a user action).  Unknown tokens leave the current
    // selection unchanged.
    void set_sort_key(const QString& token);

    // Reflect the stored sort direction in the toggle (true = descending),
    // without emitting sort_direction_changed.
    void set_sort_direction(bool descending);

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

    // The width the panel takes when fully expanded, by default.  The user can
    // drag the right edge to make it wider or narrower (clamped to
    // [k_min_width, k_max_width]); the current expanded width is remembered for
    // the session.  Widened enough, the name and the sort-value column fit
    // side by side without horizontal scrolling.
    static constexpr int k_expanded_width = 290;
    static constexpr int k_min_width      = 190;
    static constexpr int k_max_width      = 640;

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

    // Emitted when the user picks a different sort key from the dropdown.
    // Carries the stable token for that key (e.g. "name", "modified").  The
    // host persists it and re-sorts the list; the side panel doesn't sort
    // itself (it doesn't have the metadata to sort by).
    void sort_key_changed(const QString& token);

    // Emitted when the user toggles ascending/descending.  true = descending.
    void sort_direction_changed(bool descending);

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

    // Keep the right-edge resize grip pinned to the panel's edge as the panel
    // resizes (including during the slide animation).
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuild_list(QListWidget* list, const std::vector<std::string>& names);

    // Size the song list's columns so their full content is reachable: the
    // last visible column is grown to fill leftover width (no stray boundary
    // line), or to its content width when that's wider (so the horizontal
    // scrollbar can pan to it).  Runs on populate and on resize.
    void fit_song_columns();

    QTreeWidget* songs_     = nullptr;   // two columns: name | sort value
    QListWidget* playlists_ = nullptr;
    QComboBox*   sort_combo_ = nullptr;
    QToolButton* dir_btn_    = nullptr;   // ascending / descending toggle
    // Floating "+" buttons, parented to the corresponding list
    // widgets (NOT to the side_panel).  Kept as side_panel members
    // so the eventFilter can find them by which list raised the
    // resize event.
    QToolButton* songs_add_btn_     = nullptr;
    QToolButton* playlists_add_btn_ = nullptr;
    QPropertyAnimation* anim_ = nullptr;
    bool expanded_ = false;

    // Right-edge drag grip and the state captured when a drag begins.  The
    // panel's expanded width is user-adjustable and remembered here so the
    // next expand animation returns to the width the user chose.
    QWidget* resize_grip_ = nullptr;
    int      expanded_width_ = k_expanded_width;
    int      drag_start_x_   = 0;
    int      drag_start_w_   = 0;
};

} // namespace nashville::view
