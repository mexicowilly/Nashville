#include "side_panel.hpp"
#include <QListWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QAbstractScrollArea>
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

// Canonical song-list sort options, in display order.  Each entry pairs a
// stable token (persisted in the database's metadata and sent to the host via
// sort_key_changed) with the label shown in the dropdown.  The tokens are the
// contract between this dropdown and main_window's comparator — keep the two
// in step.  "name" is the default.
namespace
{
struct sort_option { const char* token; const char* label; };
const sort_option k_sort_options[] = {
    { "name",         "Name"         },
    { "authors",      "Authors"      },
    { "performer",    "Performer"    },
    { "album",        "Album"        },
    { "release_date", "Release date" },
    { "notes",        "Notes"        },
    { "created",      "Created"      },
    { "modified",     "Modified"     },
};

// Per-item data role holding the secondary value shown to the right of the
// name (the current sort key's value for that song).  Now carried in the
// tree's second column, but kept as a named constant for clarity.
// (No custom delegate is needed: QTreeWidget gives real, resizable columns.)
} // namespace

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
static void reposition_add_button(QToolButton* btn, QAbstractScrollArea* list,
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

    // Sort-by dropdown.  The panel doesn't sort the list itself (it has only
    // names, not the metadata to sort by) — it just lets the user pick a key
    // and emits sort_key_changed.  The host reads the metadata, sorts, and
    // pushes the ordered names back in.
    {
        auto* sort_row = new QHBoxLayout;
        sort_row->setContentsMargins(0, 0, 0, 0);
        auto* sort_label = new QLabel(tr("Sort by"), this);
        // Black, like the rest of the paper UI (inherits the palette's
        // WindowText) — no grey, which read as washed-out on the white panel.
        sort_combo_ = new QComboBox(this);
        for (const auto& opt : k_sort_options)
            sort_combo_->addItem(tr(opt.label), QString::fromLatin1(opt.token));
        connect(sort_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (idx < 0) return;
                emit sort_key_changed(sort_combo_->itemData(idx).toString());
            });

        // Ascending / descending toggle.  Checked == descending.  The glyph
        // points the way the values run: ▲ small-to-large, ▼ large-to-small.
        dir_btn_ = new QToolButton(this);
        dir_btn_->setCheckable(true);
        dir_btn_->setAutoRaise(true);
        dir_btn_->setText(QString::fromUtf8("\u25B2"));   // ▲ ascending
        dir_btn_->setToolTip(tr("Ascending (click for descending)"));
        connect(dir_btn_, &QToolButton::toggled, this, [this](bool desc) {
            dir_btn_->setText(QString::fromUtf8(desc ? "\u25BC" : "\u25B2"));
            dir_btn_->setToolTip(desc ? tr("Descending (click for ascending)")
                                      : tr("Ascending (click for descending)"));
            emit sort_direction_changed(desc);
        });

        sort_row->addWidget(sort_label);
        sort_row->addWidget(sort_combo_, /*stretch=*/1);
        sort_row->addWidget(dir_btn_);
        vl->addLayout(sort_row);
    }

    songs_ = new QTreeWidget(this);
    songs_->setColumnCount(2);
    songs_->setHeaderLabels({ tr("Name"), QString() });
    songs_->setRootIsDecorated(false);   // flat list, no expand triangles
    songs_->setIndentation(0);
    songs_->setUniformRowHeights(true);
    songs_->setSelectionBehavior(QAbstractItemView::SelectRows);
    songs_->setAlternatingRowColors(false);
    // Don't truncate values with an ellipsis — let them run their full length
    // so the horizontal scrollbar (and a widened column) can reveal them.
    songs_->setTextElideMode(Qt::ElideNone);
    songs_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    songs_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    songs_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Columns are user-resizable (the divider between the two header sections
    // is the "resizer"); the last section is NOT stretched to fit, so a column
    // can be dragged wider than the panel and scrolled to.
    songs_->header()->setSectionResizeMode(QHeaderView::Interactive);
    songs_->header()->setStretchLastSection(false);
    songs_->setColumnWidth(0, 110);
    songs_->setColumnWidth(1, 95);

    // CustomContextMenu so we can build a per-item right-click menu
    // ourselves (the default would give a stock menu we don't want).
    songs_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(songs_, &QTreeWidget::customContextMenuRequested,
        [this](const QPoint& pos) {
            auto* item = songs_->itemAt(pos);
            if (!item) return;
            // Select the right-clicked item so the visual cue matches the
            // context.
            songs_->setCurrentItem(item);
            const QString name = item->text(0);
            QMenu menu(this);
            QAction* del = menu.addAction(tr("Delete \"%1\"...").arg(name));
            connect(del, &QAction::triggered,
                [this, name]() { emit delete_song_requested(name); });
            menu.exec(songs_->viewport()->mapToGlobal(pos));
        });
    vl->addWidget(songs_, /*stretch=*/2);     // songs gets more space

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

    // Reposition the "+" when either scrollbar appears or disappears.  Widening
    // a column (fit_song_columns) makes the horizontal scrollbar appear without
    // resizing the list itself, so the resize-driven repositioning above never
    // fires — but the scrollbar's range does change, and by the time that
    // signal arrives the viewport geometry has settled, so the button lands
    // just above the scrollbar instead of behind it.
    connect(songs_->horizontalScrollBar(), &QScrollBar::rangeChanged, this,
        [this]() {
            if (songs_add_btn_) reposition_add_button(songs_add_btn_, songs_);
        });
    connect(songs_->verticalScrollBar(), &QScrollBar::rangeChanged, this,
        [this]() {
            if (songs_add_btn_) reposition_add_button(songs_add_btn_, songs_);
        });

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
    connect(songs_, &QTreeWidget::itemDoubleClicked,
        [this](QTreeWidgetItem* item, int) {
            if (item) emit song_double_clicked(item->text(0));
        });
    connect(songs_, &QTreeWidget::itemActivated,
        [this](QTreeWidgetItem* item, int) {
            if (item) emit song_double_clicked(item->text(0));
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
    // helper for the rationale.  We set the FULL light set, not just
    // Window/Base/Text: button-like widgets (the sort combo, the
    // direction toggle) draw from Button/ButtonText and the Light/Mid/
    // Dark shading roles, so without these they'd inherit the dark
    // theme's grey button colours against the white panel.
    QPalette pal = palette();
    pal.setColor(QPalette::Window,       Qt::white);
    pal.setColor(QPalette::Base,         Qt::white);
    pal.setColor(QPalette::Text,         Qt::black);
    pal.setColor(QPalette::WindowText,   Qt::black);
    pal.setColor(QPalette::Button,       Qt::white);
    pal.setColor(QPalette::ButtonText,   Qt::black);
    pal.setColor(QPalette::Light,        QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::Midlight,     QColor(0xf0, 0xf0, 0xf0));
    pal.setColor(QPalette::Mid,          QColor(0xc8, 0xc8, 0xc8));
    pal.setColor(QPalette::Dark,         QColor(0xa0, 0xa0, 0xa0));
    pal.setColor(QPalette::Shadow,       QColor(0x80, 0x80, 0x80));
    // Selection: a light blue with black text, matching the "+" buttons and
    // keeping both columns readable on the white panel (rather than the dark
    // theme's saturated highlight).
    pal.setColor(QPalette::Highlight,        QColor(0xd6, 0xe9, 0xff));
    pal.setColor(QPalette::HighlightedText,  Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);
}

void side_panel::set_song_names(const std::vector<std::string>& names)
{
    std::vector<std::pair<std::string, std::string>> rows;
    rows.reserve(names.size());
    for (const auto& n : names)
        rows.emplace_back(n, std::string{});
    set_song_rows(rows);
}

void side_panel::set_song_rows(
    const std::vector<std::pair<std::string, std::string>>& rows)
{
    // Clear-and-refill, preserving the selected name when it's still present.
    // Column 0 is the name; column 1 is the current sort key's value for that
    // song (blank when sorting by name).  Column widths are left untouched so
    // a user's manual resize survives a refresh.
    const QString prev_selected = songs_->currentItem()
        ? songs_->currentItem()->text(0)
        : QString();

    songs_->clear();
    QTreeWidgetItem* to_select = nullptr;
    for (const auto& row : rows)
    {
        const QString name = QString::fromStdString(row.first);
        auto* item = new QTreeWidgetItem(songs_);
        item->setText(0, name);
        item->setText(1, QString::fromStdString(row.second));
        if (!prev_selected.isEmpty() && name == prev_selected)
            to_select = item;
    }
    if (to_select)
        songs_->setCurrentItem(to_select);

    fit_song_columns();
}

void side_panel::fit_song_columns()
{
    if (!songs_)
        return;
    // The last visible column carries the "rest of the width".  Single column
    // (sort by name): that's the name column, which should fill the panel so
    // there's no boundary line down an empty right-hand strip.  Two columns:
    // it's the value column.  In both cases we set it to whichever is larger —
    // the leftover width (so it fills) or its own content width (so a long
    // value overflows and the horizontal scrollbar can reach it).
    const bool two_col = !songs_->isColumnHidden(1);
    const int  last    = two_col ? 1 : 0;
    const int  used    = two_col ? songs_->columnWidth(0) : 0;
    const int  avail   = songs_->viewport()->width() - used;
    // Measure the column's content width via the public resize call, then keep
    // whichever is larger — the leftover width (fill) or the content (scroll).
    songs_->resizeColumnToContents(last);
    const int content = songs_->columnWidth(last);
    const int w       = qMax(avail, content);
    if (w > 0)
        songs_->setColumnWidth(last, w);
}

void side_panel::set_playlist_names(const std::vector<std::string>& names)
{
    rebuild_list(playlists_, names);
}

void side_panel::set_sort_direction(bool descending)
{
    if (!dir_btn_)
        return;
    // Reflecting stored state, not a user action — don't re-emit.
    QSignalBlocker block(dir_btn_);
    dir_btn_->setChecked(descending);
    dir_btn_->setText(QString::fromUtf8(descending ? "\u25BC" : "\u25B2"));
    dir_btn_->setToolTip(descending ? tr("Descending (click for ascending)")
                                    : tr("Ascending (click for descending)"));
}

void side_panel::set_sort_key(const QString& token)
{
    if (!sort_combo_)
        return;
    const int idx = sort_combo_->findData(token);
    if (idx < 0)
        return;  // unknown token — leave the current selection alone
    // Reflecting host/database state, not a user action, so don't re-emit
    // sort_key_changed (which would trigger a redundant persist + re-sort).
    {
        QSignalBlocker block(sort_combo_);
        sort_combo_->setCurrentIndex(idx);
    }

    // The second column shows that key's value, so label its header with the
    // key and hide the column entirely when sorting by name (the value would
    // just repeat the name, and a single column needs no divider).  When the
    // value column reappears, restore the name column to a sensible default —
    // it may have been stretched to fill the panel while it was the only
    // column.  fit_song_columns() (run after the rows are set) then sizes the
    // value column.
    if (songs_)
    {
        const bool by_name = (token == "name");
        songs_->setColumnHidden(1, by_name);
        songs_->headerItem()->setText(1, by_name ? QString()
                                                  : sort_combo_->itemText(idx));
        if (!by_name)
            songs_->setColumnWidth(0, 110);
    }
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
    return item ? item->text(0) : QString();
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
        {
            // Re-fill the last column first (it may add/remove the horizontal
            // scrollbar and so change the viewport height), then place the
            // button relative to the settled viewport.
            fit_song_columns();
            reposition_add_button(songs_add_btn_, songs_);
        }
        else if (watched == playlists_ && playlists_add_btn_)
            reposition_add_button(playlists_add_btn_, playlists_);
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace nashville::view
