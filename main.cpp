#include "ui_nashville.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QMainWindow main_win;
    Ui::main_window ui;
    ui.setupUi(&main_win);
    main_win.show();
    return app.exec();
}