#pragma once

#include "ui_nashville.h"

namespace nashville::view { class song_widget; }

namespace nashville
{

class app
{
public:
    app(int argc, char* argv[]);

    QWidget* central_widget();
    int run();
    QString title() const;

private:
    // Wires the actions in the "Bar" menu (Repeat submenu + Voltas...)
    // to the song_widget's body.  Called from the constructor after
    // song_widget_ is created; pulled out into its own helper to keep
    // the constructor's data-setup code readable and to keep all the
    // QActionGroup / aboutToShow plumbing in one place.
    void wire_bar_menu();

    // Wires the three Insert actions in the "Song" menu (text box,
    // line, arrow) to the body widget's annotation tool mode.  Same
    // one-place rationale as wire_bar_menu.
    void wire_song_menu();

    QApplication qapp_;
    QMainWindow main_win_;
    Ui::window nashville_win_;
    QString title_;
    view::song_widget* song_widget_ = nullptr;
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
