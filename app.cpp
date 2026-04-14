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
    s->tempo(std::make_tuple(88, model::chord::time::EIGHTH));
    model::time_signature ts;
    ts.parse_user_input("8/8");
    s->time_sig(ts);
    s->add_bar().parse_user_input("1 sp:4").section("I");
    s->add_bar().parse_user_input("57");
    s->add_bar().parse_user_input("dt:1");
    s->add_bar().parse_user_input("s:1").is_eol(true);
    s->add_bar().parse_user_input("5 p:7dim7").section("V1");
    //ts.parse_user_input("6/8");
    s->add_bar().parse_user_input("4");
    s->add_bar().parse_user_input("t:1+");
    s->add_bar().parse_user_input("6-7");
    s->add_bar().parse_user_input("2-");
    s->add_bar().parse_user_input("1").is_eol(true);
    s->add_bar().parse_user_input("1:q. 2:e 3:h");
    s->add_bar().parse_user_input("4:h. p:5:q");
    s->add_bar().parse_user_input("5");
    s->add_bar().parse_user_input("6-").is_eol(true);
    s->add_bar().parse_user_input("sp:77 67").is_eol(true);
    s->add_bar().parse_user_input("5");
    s->add_bar().parse_user_input("6");
    s->add_bar().parse_user_input("7").is_eol(true);
    s->add_bar().parse_user_input("sp:1:h ptd:4:h");
    s->add_bar().parse_user_input("4").is_eol(true);
    s->add_bar().parse_user_input("5:q 4:q 3:q 4:q").is_eol(true);
    s->add_bar().parse_user_input("1 4").is_eol(true);
    s->add_bar().parse_user_input("5 1");
    vl->addWidget(new view::song_widget(*s));
    main_win_.setFocus();
    main_win_.show();
}

int app::run()
{
    return qapp_.exec();
}

}
