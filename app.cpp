#include "app.hpp"

namespace nashville
{

app::app(int argc, char* argv[])
    : qapp_(argc, argv)
{
    nashville_win_.setupUi(&main_win_);
    main_win_.setFocus();
    main_win_.show();
}

int app::run()
{
    return qapp_.exec();
}

}
