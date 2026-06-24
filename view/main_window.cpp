#include "main_window.hpp"
#include "side_panel.hpp"
#include "song_tab.hpp"
#include "modal_overlay.hpp"
#include "../database.hpp"
#include "../model/playlist.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QEnterEvent>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// tab_close_button — custom close button for tab chrome
// ---------------------------------------------------------------------------
// Paints a small "x" centred in the button.  A thin 1px circle is drawn
// around it only while the mouse is hovering, giving a subtle affordance
// without cluttering the tab bar at rest.  The x itself is always faintly
// visible so the button remains discoverable without requiring hover first.
class tab_close_button : public QAbstractButton
{
public:
    explicit tab_close_button(QWidget* parent = nullptr)
        : QAbstractButton(parent)
    {
        setFixedSize(16, 16);
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF  r      = QRectF(rect());
        const QPointF centre = r.center();
        const qreal   radius = r.width() / 2.0 - 1.5;   // 1.5px inset from edge

        // Circle: only while hovered
        if (underMouse())
        {
            p.setPen(QPen(QColor(120, 120, 120), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(centre, radius, radius);
        }

        // x glyph — only drawn while hovered; invisible at rest so the
        // tab chrome stays uncluttered until the user moves over the button.
        if (!underMouse())
            return;
        p.setPen(QPen(QColor(60, 60, 60), 1.2, Qt::SolidLine, Qt::RoundCap));

        const qreal arm = radius * 0.42;   // half-length of each arm
        p.drawLine(QPointF(centre.x() - arm, centre.y() - arm),
                   QPointF(centre.x() + arm, centre.y() + arm));
        p.drawLine(QPointF(centre.x() + arm, centre.y() - arm),
                   QPointF(centre.x() - arm, centre.y() + arm));
    }

    // Repaint on enter/leave so the circle appears/disappears instantly.
    void enterEvent(QEnterEvent* e) override { QAbstractButton::enterEvent(e); update(); }
    void leaveEvent(QEvent*      e) override { QAbstractButton::leaveEvent(e); update(); }
};

main_window::main_window(database& db, QWidget* parent)
    : QWidget(parent)
    , db_(db)
{
    // Force a white-paper background here too, matching the rest of
    // the app's print-look palette policy.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    pal.setColor(QPalette::Base,   Qt::white);
    pal.setColor(QPalette::Text,   Qt::black);
    pal.setColor(QPalette::WindowText, Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);

    // --- Layout ---
    // Outer: [side_panel] [main column]
    // Main column: [top bar with hamburger] [tab widget]
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    panel_ = new side_panel(this);
    outer->addWidget(panel_);

    auto* main_col = new QVBoxLayout;
    main_col->setContentsMargins(0, 0, 0, 0);
    main_col->setSpacing(0);
    outer->addLayout(main_col, /*stretch=*/1);

    // Top bar with the hamburger.  A thin row holding just the
    // toggle button, left-aligned.  We pad it slightly so the button
    // doesn't kiss the window edge.  Using a QHBoxLayout for the bar
    // even though it has one widget so a future right-side affordance
    // (e.g. an account menu, a sync indicator) can be added without
    // restructuring.
    auto* top_bar = new QHBoxLayout;
    top_bar->setContentsMargins(4, 4, 4, 0);
    top_bar->setSpacing(0);

    hamburger_ = new QToolButton(this);
    // The Unicode triple-bar U+2630 is the universal hamburger glyph;
    // using it avoids shipping an icon file.  Size up the font a touch
    // so the symbol is easy to hit.
    hamburger_->setText(QString::fromUtf8("\xE2\x98\xB0"));
    QFont hf = hamburger_->font();
    hf.setPointSize(hf.pointSize() + 4);
    hamburger_->setFont(hf);
    hamburger_->setToolTip(tr("Show songs and playlists"));
    hamburger_->setAutoRaise(true);   // flat until hovered — keeps
                                      // the toolbar visually quiet
                                      // alongside the chart
    connect(hamburger_, &QToolButton::clicked,
            panel_, &side_panel::toggle_animated);
    top_bar->addWidget(hamburger_);
    top_bar->addStretch(1);

    main_col->addLayout(top_bar);

    // The tab widget.  Tabs are closable; close requests come back to
    // close_tab_at where we flush-save and drop the tab.  Tabs are
    // movable so users can reorder their workspace.
    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);   // less chrome, more chart-like

    // Force white tab backgrounds regardless of OS theme.  The chart is a
    // "printed page" — every surface the user sees should read as white paper.
    // The stylesheet targets QTabBar::tab so inactive and active tabs both get
    // white backgrounds; the active tab gets a slightly heavier bottom border
    // to indicate selection without relying on a theme colour.
    // The close button is hidden here because we install our own custom
    // tab_close_button widget (below) on every new tab.
    tabs_->setStyleSheet(
        "QTabWidget::pane {"
        "    border: none;"
        "    background: white;"
        "}"
        "QTabBar::tab {"
        "    background: white;"
        "    color: black;"
        "    padding: 4px 8px 4px 10px;"
        "    border: 1px solid #c0c0c0;"
        "    border-bottom: none;"
        "    margin-right: 2px;"
        "}"
        "QTabBar::tab:selected {"
        "    background: white;"
        "    border-bottom: 2px solid white;"  // merges visually with pane
        "}"
        "QTabBar::tab:hover:!selected {"
        "    background: #f5f5f5;"
        "}"
        "QTabBar::close-button {"
        "    image: none;"        // hide the OS-provided close button image
        "    width: 0px;"         // collapse the default close-button slot
        "    height: 0px;"        // (our custom widget sits in its place)
        "}"
    );

    // Install a custom close button on every new tab.  We connect to
    // tabBar()'s tabBarClicked signal as a creation hook: Qt installs
    // our setTabButton call right after addTab returns, so we can't do
    // it from open_song directly (the index isn't stable until the tab
    // is fully inserted).  Instead we use QTabWidget::tabInserted via
    // a subclass — but since we can't subclass here, we re-install the
    // button immediately after addTab in open_song() instead (see below).
    connect(tabs_, &QTabWidget::tabCloseRequested,
            this, &main_window::close_tab_at);
    connect(tabs_, &QTabWidget::currentChanged,
        [this](int /*idx*/) {
            emit current_tab_changed(current_tab());
        });
    main_col->addWidget(tabs_, /*stretch=*/1);

    // Wire side-panel signals.  Songs open into tabs.  Playlist
    // double-click is currently a no-op (a slot is connected for
    // future use, just doesn't do anything yet).
    connect(panel_, &side_panel::song_double_clicked,
        [this](const QString& name) {
            open_song(name.toStdString());
        });
    connect(panel_, &side_panel::playlist_double_clicked,
        [](const QString& /*name*/) {
            // No-op for now.  Future: open all playlist songs as tabs.
        });

    // Add / delete from the side panel.  We forward to the same
    // confirm_delete_* / prompt_new_* methods the top-level menus
    // call so behavior is identical regardless of how the user got
    // here.  Keeping the prompts here (in main_window, not in
    // side_panel) means side_panel stays database-ignorant and
    // testable in isolation.
    connect(panel_, &side_panel::add_song_requested,
            this, &main_window::prompt_new_song);
    connect(panel_, &side_panel::add_playlist_requested,
            this, &main_window::prompt_new_playlist);
    connect(panel_, &side_panel::delete_song_requested,
        [this](const QString& name) {
            confirm_delete_song(name.toStdString());
        });
    connect(panel_, &side_panel::delete_playlist_requested,
        [this](const QString& name) {
            confirm_delete_playlist(name.toStdString());
        });

    // Populate lists on construction.  The database is already open
    // at this point (app constructs it before us), so these queries
    // return the current names.
    refresh_lists();

    // Construct the modal overlay last so it sits on top of every
    // sibling widget added by the layout.  It's hidden by default;
    // prompt/confirm/message methods show it on demand.
    overlay_ = new modal_overlay(this);
}

void main_window::refresh_lists()
{
    try
    {
        panel_->set_song_names(db_.select_song_names());
        panel_->set_playlist_names(db_.select_playlist_names());
    }
    catch (const std::exception&)
    {
        // If the DB is broken we don't want to crash the UI.  An
        // empty list is the right fallback; the user will notice
        // their songs are missing and we'll surface a real error
        // through the status bar later.
    }
}

void main_window::open_song(const std::string& name)
{
    // Already open?  Focus the existing tab.  Tabs are uniquely
    // keyed by song name because the database enforces unique song
    // names — opening "Yesterday" twice would either mean two tabs
    // editing the same row (bad) or just confusing the user.
    const int existing = find_tab_by_name(name);
    if (existing >= 0)
    {
        tabs_->setCurrentIndex(existing);
        return;
    }

    // Load from the database.  select_song throws if the name isn't
    // found; that shouldn't happen here because the only path to
    // this method is a double-click on a name we just listed, but
    // be defensive.
    std::unique_ptr<model::song> song;
    try
    {
        song = std::make_unique<model::song>(db_.select_song(name));
    }
    catch (const std::exception&)
    {
        return;
    }

    auto* tab = new song_tab(std::move(song), db_, this);
    const int idx = tabs_->addTab(tab, tab->tab_name());

    // Install our custom close button in place of the OS-provided one.
    // Qt's QTabBar::setTabButton places an arbitrary widget in the
    // RightSide button slot; wiring its clicked() to tabCloseRequested
    // reuses the same close path as the default button.
    auto* close_btn = new tab_close_button(tabs_->tabBar());
    connect(close_btn, &QAbstractButton::clicked, this, [this, close_btn]() {
        QTabBar* bar = tabs_->tabBar();
        for (int i = 0; i < bar->count(); ++i)
        {
            if (bar->tabButton(i, QTabBar::RightSide) == close_btn)
            {
                emit tabs_->tabCloseRequested(i);
                return;
            }
        }
    });
    tabs_->tabBar()->setTabButton(idx, QTabBar::RightSide, close_btn);

    tabs_->setCurrentIndex(idx);
}

void main_window::close_tab_at(int index)
{
    if (index < 0 || index >= tabs_->count())
        return;
    QWidget* w = tabs_->widget(index);
    auto* tab = qobject_cast<song_tab*>(w);
    if (tab)
        tab->flush_save();   // belt and suspenders — destructor flushes too,
                              // but flushing here means the side-panel
                              // refresh below sees the post-save state
    tabs_->removeTab(index);
    if (tab)
        tab->deleteLater();
    refresh_lists();
    emit current_tab_changed(current_tab());
}

void main_window::flush_all_tabs()
{
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (auto* tab = qobject_cast<song_tab*>(tabs_->widget(i)))
            tab->flush_save();
    }
}

void main_window::close_all_tabs_without_saving()
{
    // Walk in reverse because removeTab shifts higher indices down,
    // and so we delete in a predictable order.  For each tab,
    // suppress its destructor's flush_save before removing it from
    // the tab widget.  removeTab does NOT delete the contained
    // widget — we own its lifetime and delete it explicitly here.
    // (Qt would otherwise delete it via the parent-child cascade
    // when this widget is destroyed, but that runs at a moment we
    // don't control, possibly after the database is gone.)
    while (tabs_->count() > 0)
    {
        const int last = tabs_->count() - 1;
        QWidget* w = tabs_->widget(last);
        tabs_->removeTab(last);
        if (auto* tab = qobject_cast<song_tab*>(w))
        {
            tab->suppress_destructor_save();
            delete tab;
        }
        else
        {
            delete w;
        }
    }
}

int main_window::find_tab_by_name(const std::string& name) const
{
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (auto* tab = qobject_cast<song_tab*>(tabs_->widget(i)))
        {
            if (tab->song_name() == name)
                return i;
        }
    }
    return -1;
}

song_tab* main_window::current_tab() const
{
    if (tabs_->count() == 0)
        return nullptr;
    return qobject_cast<song_tab*>(tabs_->currentWidget());
}

std::string main_window::selected_song_name() const
{
    return panel_->selected_song_name().toStdString();
}

std::string main_window::selected_playlist_name() const
{
    return panel_->selected_playlist_name().toStdString();
}

// ---------------------------------------------------------------------------
// New-song / new-playlist / delete-song / delete-playlist flows
// ---------------------------------------------------------------------------
// These methods used to be synchronous: they would pop a QDialog,
// block until exec returned, read the result, then continue.  With
// the move to modal_overlay (which is async — the callback fires
// after the user dismisses the prompt) the continuation has to live
// inside a lambda.  The structure is the same; the dialog wrappers
// just spell as overlay->prompt_text(..., [this](result) { ... })
// instead of `auto result = prompt(...); ...`.
//
// All four methods reach for the database, refresh the side panel,
// and (for the new-song case) open the freshly-created song in a
// tab.  Validation against duplicates happens in is_usable_new_name,
// which is itself async now because it may need to show a warning
// overlay if the name collides.

// Helper: check whether `candidate` is a usable new name.  If empty,
// silently rejects (caller treats as cancel).  If duplicate of an
// existing name in `existing`, shows a warning overlay and calls
// `on_done(false)` after the user dismisses it; otherwise calls
// `on_done(true)` immediately.  `kind` is "song" / "playlist" for
// the warning message wording.
static void is_usable_new_name(modal_overlay* overlay,
                               const QString& candidate,
                               const std::vector<std::string>& existing,
                               const QString& kind,
                               std::function<void(bool)> on_done)
{
    const QString trimmed = candidate.trimmed();
    if (trimmed.isEmpty())
    {
        on_done(false);
        return;
    }
    for (const auto& e : existing)
    {
        if (QString::fromStdString(e) == trimmed)
        {
            // Show the warning, then call back with false once
            // the user dismisses.  Capturing on_done by value
            // (copy) means the lambda owns its own copy of the
            // closure; the calling stack frame can return before
            // the user clicks OK.
            overlay->message(
                QObject::tr("Name already in use"),
                QObject::tr("A %1 named \"%2\" already exists. "
                            "Please choose a different name.")
                    .arg(kind, trimmed),
                [on_done]() { on_done(false); });
            return;
        }
    }
    on_done(true);
}

void main_window::prompt_new_song()
{
    overlay_->prompt_text(
        tr("New song"),
        tr("Song name:"),
        [this](std::optional<QString> name) {
            if (!name) return;  // user cancelled
            // Validate.  is_usable_new_name is async (it may show
            // a warning overlay on duplicate names), so the
            // database write has to happen inside its callback.
            is_usable_new_name(overlay_, *name,
                db_.select_song_names(), tr("song"),
                [this, name](bool ok) {
                    if (!ok) return;
                    try
                    {
                        model::song s(name->toStdString());
                        db_.insert_song(s);
                    }
                    catch (const std::exception& e)
                    {
                        overlay_->message(tr("Could not create song"),
                                          QString::fromUtf8(e.what()));
                        return;
                    }
                    refresh_lists();
                    open_song(name->toStdString());
                });
        });
}

void main_window::prompt_new_playlist()
{
    overlay_->prompt_text(
        tr("New playlist"),
        tr("Playlist name:"),
        [this](std::optional<QString> name) {
            if (!name) return;
            is_usable_new_name(overlay_, *name,
                db_.select_playlist_names(), tr("playlist"),
                [this, name](bool ok) {
                    if (!ok) return;
                    try
                    {
                        model::playlist pl(name->toStdString());
                        db_.insert_playlist(pl);
                    }
                    catch (const std::exception& e)
                    {
                        overlay_->message(tr("Could not create playlist"),
                                          QString::fromUtf8(e.what()));
                        return;
                    }
                    refresh_lists();
                });
        });
}

void main_window::confirm_delete_song(const std::string& name)
{
    if (name.empty()) return;

    const QString qname = QString::fromStdString(name);
    overlay_->confirm(
        tr("Delete song"),
        tr("Delete the song \"%1\"?\n\nThis cannot be undone.").arg(qname),
        [this, name](bool yes) {
            if (!yes) return;

            // If the song has an open tab, the tab's auto-save would
            // re-insert the row we're about to delete.  We delete
            // the DB row first, then close the tab; the tab's
            // destructor will try to flush_save but we re-delete
            // after via a queued invoke to cancel that race.  See
            // the old confirm_delete_song for the longer comment.
            const int existing = find_tab_by_name(name);
            try
            {
                db_.remove_song(name);
            }
            catch (const std::exception& e)
            {
                overlay_->message(tr("Could not delete song"),
                                  QString::fromUtf8(e.what()));
                return;
            }
            if (existing >= 0)
            {
                QWidget* w = tabs_->widget(existing);
                tabs_->removeTab(existing);
                if (auto* tab = qobject_cast<song_tab*>(w))
                {
                    // Suppress the destructor's flush_save so the
                    // tab can't re-insert the row we just deleted.
                    // This is the simpler half of the cleanup;
                    // before this method gained suppress_destructor_
                    // save we had to queue a follow-up re-delete on
                    // the event loop.
                    tab->suppress_destructor_save();
                    tab->deleteLater();
                }
            }
            refresh_lists();
            emit current_tab_changed(current_tab());
        });
}

void main_window::confirm_delete_playlist(const std::string& name)
{
    if (name.empty()) return;

    const QString qname = QString::fromStdString(name);
    overlay_->confirm(
        tr("Delete playlist"),
        tr("Delete the playlist \"%1\"?\n\nThis cannot be undone.").arg(qname),
        [this, name](bool yes) {
            if (!yes) return;
            try
            {
                db_.remove_playlist(name);
            }
            catch (const std::exception& e)
            {
                overlay_->message(tr("Could not delete playlist"),
                                  QString::fromUtf8(e.what()));
                return;
            }
            refresh_lists();
        });
}

} // namespace nashville::view
