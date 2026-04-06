#include "app.hpp"
//#include "widgets/song.hpp"
#include "view/song_widget.hpp"
#include <QVBoxLayout>

namespace nashville
{

app::app(int argc, char* argv[])
    : qapp_(argc, argv)
{
    nashville_win_.setupUi(&main_win_);
    auto vl = new QVBoxLayout;
    central_widget()->setLayout(vl);
    vl->setAlignment(Qt::AlignTop);
    model::song* s = new model::song("Check It");
    s->key("G");
    s->tempo(std::make_tuple(88, model::chord::time::QUARTER));
    model::time_signature ts;
    ts.parse_user_input("4/4");
    s->time_sig(ts);
    s->add_bar().parse_user_input("1 4");
    s->add_bar().parse_user_input("57");
    s->add_bar().parse_user_input("1");
    s->add_bar().parse_user_input("1");
    s->add_bar().parse_user_input("5 7dim7");
    ts.parse_user_input("6/8");
    s->add_bar().parse_user_input("4").time_sig(ts);
    s->add_bar().parse_user_input("1+");
    s->add_bar().parse_user_input("6-7");
    s->add_bar().parse_user_input("1");
    vl->addWidget(new view::SongWidget(*s));
    main_win_.setFocus();
    main_win_.show();
}

int app::run()
{
    return qapp_.exec();
}

}
