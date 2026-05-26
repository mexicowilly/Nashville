#pragma once

#include "ui_nashville.h"
#include <memory>

namespace nashville
{

class database;

namespace view
{
class main_window;
class song_tab;
class song_body_widget;
}

class app
{
public:
    app(int argc, char* argv[]);
    ~app();

    QWidget* central_widget();
    int run();
    QString title() const;

private:
    // Wires the actions in the "Bar" menu (Repeat submenu + Voltas...)
    // to whatever the currently-focused tab's body widget is.  The
    // wiring captures `this`, not a body pointer, because the body
    // changes with each tab switch.  Each lambda looks up the
    // current tab through main_window_->current_tab() at call time;
    // if there's no current tab the action is a no-op.
    void wire_bar_menu();

    // Wires the three Insert actions in the "Song" menu (text box,
    // line, arrow) to the current tab's body widget.  Same dynamic-
    // lookup pattern as wire_bar_menu.  Also wires the Song menu's
    // New and Delete items, which go through main_window's prompt
    // and confirm methods (not the body widget).
    void wire_song_menu();

    // Wires the "New" and "Delete" actions in the Playlist menu.
    // Both go through main_window since playlists don't have a
    // per-tab editor — they're list-only in the side panel for now.
    void wire_playlist_menu();

    // Returns the song_body_widget of the currently focused tab, or
    // nullptr if no tab is open.  Used by the menu wiring lambdas to
    // retarget on each invocation.
    view::song_body_widget* current_body() const;

    QApplication qapp_;
    QMainWindow main_win_;
    Ui::window nashville_win_;
    QString title_;

    // The application's single database.  Owned here; passed by
    // reference to main_window for population of song / playlist
    // lists, and to each song_tab for save-on-edit.  Order in the
    // member list matters: db_ must be constructed before
    // main_window_ (which references it) and destroyed after
    // (so any final save on app quit can hit the live database).
    std::unique_ptr<database>  db_;
    view::main_window*         main_window_ = nullptr;
};

inline QWidget* app::central_widget()
{
    return nashville_win_.central_widget;
}

inline QString app::title() const
{
    return title_;
}

}
