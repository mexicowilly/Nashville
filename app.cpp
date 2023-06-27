#include "app.hpp"
#include <QGridLayout>
#include <QLineEdit>

namespace nashville
{

app::app(int argc, char* argv[])
    : qapp_(argc, argv)
{
    nashville_win_.setupUi(&main_win_);

    auto title = new QLineEdit();
    title->setPlaceholderText("Title");
    title->setFrame(false);
    title->setReadOnly(true);
    nashville_win_.grid_layout->addWidget(title, 0, 0, 1, 2, Qt::AlignCenter);
    auto title2 = new QLineEdit();
    title2->setPlaceholderText("two");
    nashville_win_.grid_layout->addWidget(title2, 1, 0, Qt::AlignLeft);
    auto title3 = new QLineEdit();
    title3->setPlaceholderText("three");
    nashville_win_.grid_layout->addWidget(title3, 1, 1, Qt::AlignLeft);

    main_win_.show();
}

int app::run()
{
    return qapp_.exec();
}

}