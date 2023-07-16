#include "app.hpp"
#include "title_text.hpp"

namespace nashville
{

app::app(int argc, char* argv[])
    : qapp_(argc, argv)
{
    nashville_win_.setupUi(&main_win_);

    auto title = new ui::title_text(central_widget());

    main_win_.setFocus();
    main_win_.show();
}

int app::run()
{
    return qapp_.exec();
}

}