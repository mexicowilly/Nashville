#include "side_panel.hpp"
#include <QListWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPalette>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QEvent>
#include <QScrollBar>

namespace nashville::view
{

// Build a "+" button: a small flat tool button with a plain "+" on
// a light-blue background, with a 1px border that draws a single
// circle around the glyph.  Earlier versions used Unicode U+2295
// CIRCLED PLUS (⊕), which already includes a circle in the glyph
// itself — combined with the button's border-radius that gave a
// confusing "circle around a circled plus" look.  A plain ASCII "+"
// inside the circular button border is unambiguous.
//
// The light-blue fill (a desaturated #d6e9ff) reads as "interactive"
// against the white panel without competing with the chart's chord-
// selection blues.  Hover and pressed states deepen the fill a step
// each, so the affordance feels responsive.
//
// The button is parented to the QListWidget it belongs to (not to
// the side panel) so it floats over the list's contents in the
// bottom-right corner; see reposition_add_button.
static QToolButton* make_add_button(QWidget* parent, const QString& tip)
{
    auto* b = new QToolButton(parent);
    b->setText(QStringLiteral("+"));
    QFont f = b->font();
    f.setPointSize(f.pointSize() + 4);
    f.setBold(true);
    b->setFont(f);
    b->setToolTip(tip);
    b->setFixedSize(24, 24);
    b->setStyleSheet(
        "QToolButton {"
        "  background-color: #d6e9ff;"
        "  border: 1px solid #6699cc;"
        "  border-radius: 12px;"
        "  color: black;"
        "  padding: 0px;"
        "}"
        "QToolButton:hover {"
        "  background-color: #b8d6f5;"
        "}"
        "QToolButton:pressed {"
        "  background-color: #99c2eb;"
        "}");
    b->setAutoRaise(false);
    return b;
}

// Move `btn` to the bottom-right corner of `list`, with `margin`
// pixels of padding on each side.  Accounts for the list's vertical
// scrollbar if it's visible — without this the button can overlap
// the scrollbar thumb and become hard to click.  Called from the
// list's eventFilter on resize and on scrollbar visibility changes.
static void reposition_add_button(QToolButton* btn, QListWidget* list,
                                  int margin = 6)
{
    int x = list->viewport()->width()  - btn->width()  - margin;
    int y = list->viewport()->height() - btn->height() - margin;
    // Convert from viewport coords to list coords.  The viewport's
    // top-left is offset from the list's top-left by the frame
    // thickness; mapTo handles that for us.
    QPoint top_left = list->viewport()->mapTo(list, QPoint(x, y));
    btn->move(top_left);
    btn->raise();   // ensure on top of any item delegates that paint
                    // into the corner area
}

side_panel::side_panel(QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(4);

    QFont section_font;     // applied to both section labels below

    // --- Songs section ---
    // Section label only — the "+" button is no longer in the header.
    // It floats inside the list widget itself, bottom-right corner,
    // matching the FAB (floating-action-button) pattern.  The list
    // is now visually the entire affordance for that section: rows
    // for selection, "+" in the corner for "add a new one."
    auto* songs_label = new QLabel(tr("Songs"), this);
    section_font = songs_label->font();
    section_font.setBold(true);
    songs_label->setFont(section_font);
    vl->addWidget(songs_label);

    songs_ = new QListWidget(this);
    songs_->setAlternatingRowColors(false);  // alternating rows look
                                             // weird against the white
                                             // print palette we force
                                             // on the rest of the app
    // CustomContextMenu so we can build a per-item right-click menu
    // ourselves (the default DefaultContextMenu would give a stock
    // QListWidget menu we don't want).
    songs_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(songs_, &QListWidget::customContextMenuRequested,
        [this](const QPoint& pos) {
            auto* item = songs_->itemAt(pos);
            if (!item) return;
            // Select the right-clicked item so the visual cue
            // matches the context.  Without this the menu can pop
            // up over a row that doesn't appear selected, which is
            // disorienting.
            songs_->setCurrentItem(item);
            const QString name = item->text();
            QMenu menu(this);
            QAction* del = menu.addAction(tr("Delete \"%1\"...").arg(name));
            connect(del, &QAction::triggered,
                [this, name]() { emit delete_song_requested(name); });
            menu.exec(songs_->mapToGlobal(pos));
        });
    vl->addWidget(songs_, /*stretch=*/2);     // songs gets more space
                                              // than playlists by
                                              // default — most users
                                              // have far more songs
                                              // than playlists

    // Floating "+" button for songs.  Parented to the list so it
    // sits inside the list's bounds.  Initial position is set to
    // somewhere reasonable; the real position comes from the
    // eventFilter the first time the list is resized.
    auto* add_song_btn = make_add_button(songs_, tr("New song..."));
    add_song_btn->show();
    connect(add_song_btn, &QToolButton::clicked,
            this, &side_panel::add_song_requested);
    // Install an event filter on the list to catch resize events so
    // we can reposition the button.  Filter target is the list
    // itself, not the viewport — viewport resize is a subset of
    // list resize and the list is enough.  We capture the button
    // pointer so the filter knows which child to move.
    songs_->installEventFilter(this);
    songs_add_btn_ = add_song_btn;

    // --- Playlists section ---
    auto* playlists_label = new QLabel(tr("Playlists"), this);
    playlists_label->setFont(section_font);
    vl->addWidget(playlists_label);

    playlists_ = new QListWidget(this);
    playlists_->setAlternatingRowColors(false);
    playlists_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(playlists_, &QListWidget::customContextMenuRequested,
        [this](const QPoint& pos) {
            auto* item = playlists_->itemAt(pos);
            if (!item) return;
            playlists_->setCurrentItem(item);
            const QString name = item->text();
            QMenu menu(this);
            QAction* del = menu.addAction(tr("Delete \"%1\"...").arg(name));
            connect(del, &QAction::triggered,
                [this, name]() { emit delete_playlist_requested(name); });
            menu.exec(playlists_->mapToGlobal(pos));
        });
    vl->addWidget(playlists_, /*stretch=*/1);

    auto* add_playlist_btn = make_add_button(playlists_, tr("New playlist..."));
    add_playlist_btn->show();
    connect(add_playlist_btn, &QToolButton::clicked,
            this, &side_panel::add_playlist_requested);
    playlists_->installEventFilter(this);
    playlists_add_btn_ = add_playlist_btn;

    // Double-click handlers.  QListWidget emits itemDoubleClicked
    // (not itemActivated) on a literal double-click — itemActivated
    // also fires on Enter, which would let a list-focused user trigger
    // an open with the keyboard.  We want the keyboard path too, so
    // wire both.  Both ultimately dispatch through the same signal
    // since the host can't tell the difference and shouldn't care.
    connect(songs_, &QListWidget::itemDoubleClicked,
        [this](QListWidgetItem* item) {
            if (item) emit song_double_clicked(item->text());
        });
    connect(songs_, &QListWidget::itemActivated,
        [this](QListWidgetItem* item) {
            if (item) emit song_double_clicked(item->text());
        });
    connect(playlists_, &QListWidget::itemDoubleClicked,
        [this](QListWidgetItem* item) {
            if (item) emit playlist_double_clicked(item->text());
        });
    connect(playlists_, &QListWidget::itemActivated,
        [this](QListWidgetItem* item) {
            if (item) emit playlist_double_clicked(item->text());
        });

    // Animation setup.  We animate maximumWidth so the parent layout
    // (a QHBoxLayout in main_window) resizes the side panel as we
    // change it.  minimumWidth follows the same value so the panel
    // doesn't have a fixed width that fights the animation.  Easing
    // curve OutCubic gives a snappy start and a soft stop, which
    // reads as "responsive" without being jarring — same curve used
    // by most slide-out drawers in mobile UIs.
    setMinimumWidth(0);
    setMaximumWidth(0);
    anim_ = new QPropertyAnimation(this, "maximumWidth", this);
    anim_->setDuration(200);
    anim_->setEasingCurve(QEasingCurve::OutCubic);

    // Force a white-on-light palette so the panel matches the chart's
    // print look regardless of system dark mode.  Same idiom used
    // elsewhere in the app — see app.cpp's force_white_background
    // helper for the rationale.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    pal.setColor(QPalette::Base,   Qt::white);
    pal.setColor(QPalette::Text,   Qt::black);
    pal.setColor(QPalette::WindowText, Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);
}

void side_panel::set_song_names(const std::vector<std::string>& names)
{
    rebuild_list(songs_, names);
}

void side_panel::set_playlist_names(const std::vector<std::string>& names)
{
    rebuild_list(playlists_, names);
}

// Clear-and-refill rather than diff: the names list is small (a few
// hundred at most for a working musician's library), so the cost is
// nil and the code is simpler.  Preserves selection by name when
// possible so a user-driven refresh (e.g. after closing a tab)
// doesn't lose the user's cursor position.
void side_panel::rebuild_list(QListWidget* list,
                              const std::vector<std::string>& names)
{
    const QString prev_selected = list->currentItem()
        ? list->currentItem()->text()
        : QString();

    list->clear();
    int select_row = -1;
    for (int i = 0; i < int(names.size()); ++i)
    {
        const QString s = QString::fromStdString(names[i]);
        list->addItem(s);
        if (!prev_selected.isEmpty() && s == prev_selected)
            select_row = i;
    }
    if (select_row >= 0)
        list->setCurrentRow(select_row);
}

void side_panel::show_animated()
{
    if (expanded_) return;
    expanded_ = true;
    // Stop any in-flight reverse animation before reversing direction
    // — without this the animation would jump from the current width
    // back to 0 and then re-animate to expanded.  stop() leaves the
    // property at its current value, which is the right starting
    // point for the new direction.
    anim_->stop();
    anim_->setStartValue(maximumWidth());
    anim_->setEndValue(k_expanded_width);
    anim_->start();
}

void side_panel::hide_animated()
{
    if (!expanded_) return;
    expanded_ = false;
    anim_->stop();
    anim_->setStartValue(maximumWidth());
    anim_->setEndValue(0);
    anim_->start();
}

void side_panel::toggle_animated()
{
    if (expanded_) hide_animated();
    else           show_animated();
}

QString side_panel::selected_song_name() const
{
    auto* item = songs_->currentItem();
    return item ? item->text() : QString();
}

QString side_panel::selected_playlist_name() const
{
    auto* item = playlists_->currentItem();
    return item ? item->text() : QString();
}

bool side_panel::eventFilter(QObject* watched, QEvent* event)
{
    // Reposition the floating "+" button when its parent list is
    // resized (which happens on side-panel resize, on app window
    // resize, and on the side-panel slide-in/out animation while
    // the panel's maximumWidth is changing).  We also reposition
    // on Show — the very first resize event fires before show, but
    // the layout may not be settled until show; doing both is
    // cheap and covers the common cases robustly.
    if (event->type() == QEvent::Resize ||
        event->type() == QEvent::Show)
    {
        if (watched == songs_ && songs_add_btn_)
            reposition_add_button(songs_add_btn_, songs_);
        else if (watched == playlists_ && playlists_add_btn_)
            reposition_add_button(playlists_add_btn_, playlists_);
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace nashville::view
