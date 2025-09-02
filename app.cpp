#include "app.hpp"
#include "widgets/song.hpp"

namespace nashville
{

app::app(int argc, char* argv[])
    : qapp_(argc, argv)
{
    nashville_win_.setupUi(&main_win_);
    auto vl = new QVBoxLayout;
    central_widget()->setLayout(vl);
    vl->setAlignment(Qt::AlignTop);
    vl->addWidget(new widgets::song);
    main_win_.setFocus();
    main_win_.show();
}

int app::run()
{
    return qapp_.exec();
}

}
