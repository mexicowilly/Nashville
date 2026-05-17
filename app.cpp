#include "app.hpp"
//#include "widgets/song.hpp"
#include "view/song_widget.hpp"
#include "view/song_body_widget.hpp"
#include <QVBoxLayout>
#include <QActionGroup>

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
    s->add_bar().parse_user_input("1 sp:4").section("I").repeat(model::bar::repeat_status::BEGIN);
    s->add_bar().parse_user_input("57");
    s->add_bar().parse_user_input("d:1").add_volta(0);
    s->add_bar().parse_user_input("s:1").add_volta(1).is_eol(true);
    s->add_bar().parse_user_input("5 p:7dim7").section("V1");
    s->add_bar().parse_user_input("1");
    song_widget_ = new view::song_widget(*s);
    vl->addWidget(song_widget_);
    wire_bar_menu();
    main_win_.setFocus();
    main_win_.show();
}

// ---------------------------------------------------------------------------
// wire_bar_menu
// ---------------------------------------------------------------------------
// The "Bar" menu's actions act on whichever bars are currently selected
// in the chart, so all wiring goes through song_widget_->body().  The
// three Repeat items are grouped into an exclusive QActionGroup so only
// one shows a checkmark at a time, and an aboutToShow handler on the
// Repeat submenu re-syncs those checkmarks from the live selection
// before the menu is shown — otherwise they'd drift out of date as the
// user changes selection or re-edits bars via other paths.
//
// When the selection is heterogeneous (different bars with different
// repeat states), all three Repeat items go unchecked: there's no
// single right answer to highlight, and forcing one would mislead the
// user into thinking the selection was uniform.  Clicking any item in
// that state still works and collapses the heterogeneity.
//
// Disabling the whole menu when nothing is selected mirrors the gating
// the right-click path already enforces (contextMenuEvent does nothing
// when the click misses a bar): both routes lead to the same operations
// and should agree about when they're available.
void app::wire_bar_menu()
{
    using repeat_status = model::bar::repeat_status;

    auto* body = song_widget_->body();

    // Mutually exclusive group for None/Begin/End.  Parented to the
    // QMainWindow so it lives as long as the actions themselves.
    auto* group = new QActionGroup(&main_win_);
    group->setExclusive(true);
    group->addAction(nashville_win_.actionNone);
    group->addAction(nashville_win_.actionBegin);
    group->addAction(nashville_win_.actionEnd);

    QObject::connect(nashville_win_.actionNone, &QAction::triggered,
        [body]() { body->apply_repeat_to_selection(repeat_status::NONE); });
    QObject::connect(nashville_win_.actionBegin, &QAction::triggered,
        [body]() { body->apply_repeat_to_selection(repeat_status::BEGIN); });
    QObject::connect(nashville_win_.actionEnd, &QAction::triggered,
        [body]() { body->apply_repeat_to_selection(repeat_status::END); });

    QObject::connect(nashville_win_.actionVoltas, &QAction::triggered,
        [body]() { body->prompt_voltas_for_selection(); });

    QObject::connect(nashville_win_.actionInsertBefore, &QAction::triggered,
        [body]() { body->insert_bar_relative_to_selection(/*after=*/false); });
    QObject::connect(nashville_win_.actionInsertAfter, &QAction::triggered,
        [body]() { body->insert_bar_relative_to_selection(/*after=*/true); });
    QObject::connect(nashville_win_.actionEndLine, &QAction::triggered,
        [body]() { body->apply_end_line_to_selection(); });

    // Sync the Repeat submenu's checkmarks and the "Bar" menu's overall
    // enabled state whenever the menus are about to appear.  Doing the
    // sync lazily (on aboutToShow rather than on every selection change)
    // means we don't have to broadcast selection-change signals from the
    // body widget — selection state is queried at the moment it's
    // displayed and nowhere else.  All Bar-menu items require a
    // selection: insertion needs an anchor bar, and the attribute edits
    // need targets — so they're gated together.
    QObject::connect(nashville_win_.menuBar, &QMenu::aboutToShow,
        [this, body]() {
            const bool sel = body->has_selection();
            nashville_win_.actionInsertBefore->setEnabled(sel);
            nashville_win_.actionInsertAfter->setEnabled(sel);
            nashville_win_.actionEndLine->setEnabled(sel);
            nashville_win_.menuRepeat->setEnabled(sel);
            nashville_win_.actionVoltas->setEnabled(sel);
        });

    QObject::connect(nashville_win_.menuRepeat, &QMenu::aboutToShow,
        [this, body]() {
            // Re-sync checkmarks from the live selection.  Programmatic
            // setChecked is not affected by the QActionGroup's
            // exclusion policy (that policy only governs user input),
            // so we can clear and reset freely.  When the selection is
            // heterogeneous (shared == nullopt), all three end up
            // unchecked, signalling "mixed" to the user; any subsequent
            // click then applies that choice to the whole selection.
            auto shared = body->common_repeat_of_selection();
            nashville_win_.actionNone->setChecked(
                shared.has_value() && *shared == repeat_status::NONE);
            nashville_win_.actionBegin->setChecked(
                shared.has_value() && *shared == repeat_status::BEGIN);
            nashville_win_.actionEnd->setChecked(
                shared.has_value() && *shared == repeat_status::END);
        });
}

int app::run()
{
    return qapp_.exec();
}

}
