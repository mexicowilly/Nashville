#pragma once

#include "ui_nashville.h"

namespace nashville
{

class app
{
public:
    app(int argc, char* argv[]);

    QWidget& central_widget();
    int run();
    QString title() const;

private:
    QApplication qapp_;
    QMainWindow main_win_;
    Ui::window nashville_win_;
};

inline QWidget& app::central_widget()
{
    return *nashville_win_.central_widget;
}

}