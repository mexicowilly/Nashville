#include "app.hpp"
#include "database.hpp"
#include "ui_settings.hpp"
#include "view/main_window.hpp"
#include "view/song_tab.hpp"
#include "view/song_widget.hpp"
#include "view/song_body_widget.hpp"
#include "view/page_setup_dialog.hpp"
#include <QVBoxLayout>
#include <QActionGroup>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QFont>
#include <QKeySequence>
#include <QList>
#include <QPalette>
#include <QCloseEvent>
#include <QPrinter>
#include <vector>
#include <cmath>

namespace nashville
{

// Force a white background on `w`, regardless of the desktop theme.
// The chart is meant to imply a printed page, so any pixel the user
// can see — whether painted by the song widget itself, by the scroll
// area's viewport, or by a sibling/parent — must read as white.
// QPalette::Window and QPalette::Base together cover both the
// container's chrome and any embedded "base" widgets (text inputs,
// scroll viewports) that derive their fill from the palette.
// setAutoFillBackground is required because QWidgets don't paint
// their palette background by default — they expect their painter
// to fill in paintEvent.  For passive container widgets that don't
// have a custom paintEvent, autoFill is the only way to override the
// theme's clear color.
static void force_white_background(QWidget* w)
{
    if (!w) return;
    QPalette pal = w->palette();
    pal.setColor(QPalette::Window, Qt::white);
    pal.setColor(QPalette::Base,   Qt::white);
    w->setPalette(pal);
    w->setAutoFillBackground(true);
}

app::app(int argc, char* argv[])
    : qapp_(argc, argv)
{
    nashville_win_.setupUi(&main_win_);

    // The desktop theme here is dark, so the menu bar and its dropdowns come
    // up dark and a touch oversized against the app's white "paper" look.
    // Style them the way MuseScore does: a slightly smaller font and white
    // backgrounds with black text for the bar and every menu, with a light
    // hover highlight.  The bar's stylesheet cascades to its child QMenus
    // (the dropdowns and submenus) for colours; the font is set on the bar
    // and on each menu explicitly, since popup menus don't reliably inherit
    // the bar's font.
    if (auto* mb = nashville_win_.menubar)
    {
        QFont mf = mb->font();
        if (mf.pointSizeF() > 0.0)
            mf.setPointSizeF(mf.pointSizeF() * 0.9);
        else if (mf.pixelSize() > 0)
            mf.setPixelSize(static_cast<int>(mf.pixelSize() * 0.9));
        mb->setFont(mf);
        for (QMenu* m : mb->findChildren<QMenu*>())
            m->setFont(mf);

        mb->setStyleSheet(
            "QMenuBar { background-color:#ffffff; color:#000000;"
            "           border-bottom:1px solid #d8d8d8; }"
            "QMenuBar::item { background:transparent; padding:3px 10px; }"
            "QMenuBar::item:selected { background-color:#e6e6e6; }"
            "QMenuBar::item:pressed  { background-color:#dcdcdc; }"
            "QMenu { background-color:#ffffff; color:#000000;"
            "        border:1px solid #c8c8c8; }"
            "QMenu::item { padding:4px 24px 4px 20px; }"
            "QMenu::item:selected { background-color:#e6e6e6; color:#000000; }"
            "QMenu::item:disabled { color:#b0b0b0; }"
            "QMenu::separator { height:1px; background:#e0e0e0; margin:4px 8px; }"
        );
    }

    // Open the single application-wide database.  Default-constructed
    // database picks its own on-disk location (see database.cpp);
    // future versions could honor a command-line override or a
    // standard-location lookup.  Constructed before main_window_
    // because main_window_'s constructor immediately populates the
    // song / playlist lists from it.
    db_ = std::make_unique<database>();

    // Replace the .ui's central QScrollArea contents with our own
    // composite main_window widget — hamburger + side panel + tabs.
    // The .ui declared a QScrollArea so we don't lose its layout
    // alignment, but we don't need scrolling at this level (the
    // song widget inside each tab scrolls internally).  Put a
    // QVBoxLayout on the central widget and drop the main_window
    // into it.
    auto vl = new QVBoxLayout;
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    central_widget()->setLayout(vl);
    force_white_background(central_widget());

    main_window_ = new view::main_window(*db_);
    vl->addWidget(main_window_);

    // Let main_window intercept the top-level window's close event so
    // it can guard against quitting with unsaved in-memory content —
    // see main_window::eventFilter for the full rationale. This covers
    // File > Exit, the window's own close button, and OS-level quit
    // (e.g. Cmd+Q) uniformly, since all of them arrive here as the
    // same QEvent::Close on main_win_.
    main_win_.installEventFilter(main_window_);

    wire_bar_menu();
    wire_file_menu();
    wire_song_menu();
    wire_playlist_menu();
    wire_view_menu();
    main_win_.setFocus();
    main_win_.show();

    // The database is open and the UI is fully wired: reopen the songs that
    // were in tabs when this database was last closed.  Done here (rather than
    // in main_window's constructor) so the menu wiring is already connected to
    // current_tab_changed when the restored tabs announce themselves.
    main_window_->restore_open_tabs();
}

app::~app()
{
    // Two-phase teardown.  First, flush every open tab so we don't
    // lose unsaved edits.  Then explicitly close all tabs WITHOUT
    // saving.  The explicit close is necessary because main_window_
    // is parented to main_win_ (a Qt parent-child relationship), so
    // Qt's parent-deletion cascade would otherwise destroy each tab
    // *after* db_ has been destroyed by the unique_ptr's destructor,
    // and song_tab's destructor calls flush_save which would hit a
    // dead reference.  By closing tabs explicitly here we ensure the
    // teardown order is: flush → close (no save) → db destroyed.
    if (main_window_)
    {
        main_window_->flush_all_tabs();
        // Record which songs are open BEFORE we tear the tabs down, while the
        // database is still alive.  flush_all_tabs above has assigned a row id
        // to any never-saved song, so all open songs are now persistable.
        main_window_->persist_open_tabs();
        main_window_->close_all_tabs_without_saving();
    }
}

int app::run()
{
    return qapp_.exec();
}

// Look up the currently-focused tab's body widget.  Returns nullptr
// if no tab is open — every menu lambda must handle that, since the
// app can be run with an empty database and the user can close every
// tab back to that state.
view::song_body_widget* app::current_body() const
{
    if (!main_window_) return nullptr;
    auto* tab = main_window_->current_tab();
    if (!tab) return nullptr;
    return tab->widget()->body();
}

// ---------------------------------------------------------------------------
// wire_bar_menu
// ---------------------------------------------------------------------------
// The "Bar" menu's actions act on whichever bars are currently selected
// in the chart, so all wiring goes through current_body().  The three
// Repeat items are grouped into an exclusive QActionGroup so only one
// shows a checkmark at a time, and an aboutToShow handler on the
// Repeat submenu re-syncs those checkmarks from the live selection
// before the menu is shown.
//
// Multi-tab pattern: lambdas capture `this` rather than a body
// pointer, and call current_body() each time they fire.  This means
// the menus always target the visible tab, automatically following
// tab switches without needing to rewire on every change.  Each
// lambda guards against a null body (no tabs open) by returning
// early.
//
// When the selection is heterogeneous (different bars with different
// repeat states), all three Repeat items go unchecked: there's no
// single right answer to highlight, and forcing one would mislead the
// user into thinking the selection was uniform.
//
// Disabling the whole menu when nothing is selected mirrors the
// gating the right-click path already enforces.
void app::wire_bar_menu()
{
    using repeat_status = model::bar::repeat_status;

    // BEGIN and END are now independent bits — no exclusive QActionGroup.
    // "None" clears all bits; "Begin" and "End" each toggle their own bit.
    QObject::connect(nashville_win_.actionBegin, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_repeat_to_selection(repeat_status::BEGIN); });
    QObject::connect(nashville_win_.actionEnd, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_repeat_to_selection(repeat_status::END); });

    QObject::connect(nashville_win_.actionVoltas, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->prompt_voltas_for_selection(); });

    QObject::connect(nashville_win_.actionCustomBeats, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->prompt_beats_for_selection(); });

    QObject::connect(nashville_win_.actionModulation, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->prompt_modulation_for_selection(); });

    QObject::connect(nashville_win_.actionInsertBefore, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->insert_bar_relative_to_selection(/*after=*/false); });
    QObject::connect(nashville_win_.actionInsertAfter, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->insert_bar_relative_to_selection(/*after=*/true); });
    QObject::connect(nashville_win_.actionEndLine, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_end_line_to_selection(); });

    // Delete responds to either the Del or Backspace key — both read
    // as "remove selection" to most users.  See the original wiring
    // comment for the full rationale (kept here verbatim to preserve
    // the design context).  Multi-tab note: WindowShortcut context
    // means the shortcut fires no matter which tab is focused — the
    // current_body() lookup directs the work to the right place.
    nashville_win_.actionDelete->setShortcuts(
        QList<QKeySequence>{ QKeySequence(Qt::Key_Delete),
                             QKeySequence(Qt::Key_Backspace) });
    QObject::connect(nashville_win_.actionDelete, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_delete_to_selection(); });

    // Cut/Copy/Paste use the platform-standard sequences (Ctrl+X/C/V,
    // or their platform equivalents) via QKeySequence's standard-key
    // overloads, same idea as actionPrint's QKeySequence::Print below.
    // WindowShortcut context (the QAction default) means these fire no
    // matter which tab is focused, and Qt's ShortcutOverride mechanism
    // lets an open QLineEdit (e.g. the inline bar editor) claim
    // Ctrl+C/X/V for ordinary text editing before these ever see it.
    nashville_win_.actionCut->setShortcut(QKeySequence::Cut);
    QObject::connect(nashville_win_.actionCut, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_cut_to_selection(); });
    nashville_win_.actionCopy->setShortcut(QKeySequence::Copy);
    QObject::connect(nashville_win_.actionCopy, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_copy_to_selection(); });
    nashville_win_.actionPaste->setShortcut(QKeySequence::Paste);
    QObject::connect(nashville_win_.actionPaste, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_paste_after_selection(); });

    // Undo/Redo use the platform-standard sequences too (Ctrl+Z, and
    // Ctrl+Shift+Z or Ctrl+Y depending on platform for redo). Same
    // WindowShortcut/ShortcutOverride reasoning as Cut/Copy/Paste above:
    // an open QLineEdit gets first claim on Ctrl+Z for its own text-undo
    // before this ever sees it, so undoing a bar-content edit and
    // undoing a few characters just typed into an open editor don't
    // fight over the same keystroke.
    nashville_win_.actionUndo->setShortcut(QKeySequence::Undo);
    QObject::connect(nashville_win_.actionUndo, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_undo(); });
    nashville_win_.actionRedo->setShortcut(QKeySequence::Redo);
    QObject::connect(nashville_win_.actionRedo, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->apply_redo(); });
    QObject::connect(nashville_win_.menuEdit, &QMenu::aboutToShow,
        [this]() {
            auto* b = current_body();
            nashville_win_.actionUndo->setEnabled(b && b->can_undo());
            nashville_win_.actionRedo->setEnabled(b && b->can_redo());
        });

    // Sync the Bar menu's enabled state and the Repeat submenu's
    // checkmarks lazily on aboutToShow.  Most Bar-menu items require a
    // selection AND an open tab; we gate on both. Paste is the
    // exception — it only needs a non-empty clipboard, since pasting
    // with nothing selected appends at the end of the song.
    QObject::connect(nashville_win_.menuBar, &QMenu::aboutToShow,
        [this]() {
            auto* b = current_body();
            const bool sel = b && b->has_selection();
            nashville_win_.actionInsertBefore->setEnabled(sel);
            nashville_win_.actionInsertAfter->setEnabled(sel);
            nashville_win_.actionEndLine->setEnabled(sel);
            nashville_win_.menuRepeat->setEnabled(sel);
            nashville_win_.actionVoltas->setEnabled(sel);
            nashville_win_.actionCustomBeats->setEnabled(sel);
            nashville_win_.actionModulation->setEnabled(sel);
            nashville_win_.actionDelete->setEnabled(sel);
            nashville_win_.actionCut->setEnabled(sel);
            nashville_win_.actionCopy->setEnabled(sel);
            nashville_win_.actionPaste->setEnabled(b && b->has_clipboard());
        });

    QObject::connect(nashville_win_.menuRepeat, &QMenu::aboutToShow,
        [this]() {
            auto* b = current_body();
            if (!b) {
                nashville_win_.actionBegin->setChecked(false);
                nashville_win_.actionEnd->setChecked(false);
                return;
            }
            // shared is optional<int>: nullopt = mixed, has_value = homogeneous.
            auto shared = b->common_repeat_of_selection();
            // "Begin"/"End" are checked when their bit is set.
            nashville_win_.actionBegin->setChecked(
                shared.has_value() && (*shared & repeat_status::BEGIN));
            nashville_win_.actionEnd->setChecked(
                shared.has_value() && (*shared & repeat_status::END));
        });
}

// ---------------------------------------------------------------------------
// wire_file_menu
// ---------------------------------------------------------------------------
// Open / Save / Save As for the workspace database.
//
//   * Open... (Ctrl+O)     — switch to a different database file.  If the
//                            current session is unsaved scratch, offers to
//                            save it first.
//   * Save (Ctrl+S)        — while the database is in memory (untitled),
//                            prompts for a path (it has to — there's nowhere
//                            to save yet).  Once file-backed, the database is
//                            already syncing continuously, so Save just
//                            flushes every open tab to make that durable now.
//   * Save As... (Ctrl+Shift+S) — always prompts for a path and moves the
//                            database there.
//
// Both route through main_window, which owns the flush-then-move ordering and
// the error reporting.  Exit is wired in the .ui (actionExit -> close()).
void app::wire_file_menu()
{
    QObject::connect(nashville_win_.actionOpen, &QAction::triggered,
        [this]() { main_window_->prompt_open(); });
    QObject::connect(nashville_win_.actionSave, &QAction::triggered,
        [this]() { main_window_->save(); });
    QObject::connect(nashville_win_.actionSaveAs, &QAction::triggered,
        [this]() { main_window_->save_as(); });
}

// ---------------------------------------------------------------------------
// wire_song_menu
// ---------------------------------------------------------------------------
// The Song menu now carries six actions:
//
//   * New...           — prompt for a name, insert into DB, open in tab.
//                        Routes through main_window::prompt_new_song so
//                        the menu and the side-panel "+" button share
//                        one code path.
//   * Delete           — deletes the currently-focused tab's song after
//                        a confirmation dialog.  Disabled if no tab is
//                        open, because there'd be no target.  Side-
//                        panel right-click also offers Delete, scoped
//                        to whatever row was right-clicked — different
//                        target, same confirm+delete code path.
//   * Print...         — print the currently-focused tab's chart.  The
//                        print dialog + painting live in
//                        song_widget::print; this just supplies a
//                        QPrinter for the visible tab.  Bound to Ctrl+P
//                        and disabled when no tab is open.
//   * Bars per line... — prompt for the song's preferred bars-per-line
//                        and reflow the chart's line breaks to match.
//                        Targets current_body() (it rewrites the chart's
//                        per-bar is_eol flags) and is disabled with no
//                        open tab.
//   * Insert text box / line / arrow — the existing one-shot
//                        annotation tools, unchanged.
//
// New, Delete and Print go through main_window / the tab's song_widget
// (not current_body) because they're song-level operations on the
// document, not edits to the chart's bar contents.
void app::wire_song_menu()
{
    using tool = view::annotation_layer::tool;

    QObject::connect(nashville_win_.actionNewSong, &QAction::triggered,
        [this]() { main_window_->prompt_new_song(); });
    QObject::connect(nashville_win_.actionDeleteSong, &QAction::triggered,
        [this]() {
            // Target the currently-focused tab's song.  Disabled in
            // aboutToShow below when no tab is open; this is a
            // defensive early-return for paths that bypass the menu
            // gating (keyboard shortcuts, programmatic triggers).
            if (auto* tab = main_window_->current_tab())
                main_window_->confirm_delete_song(tab->song_name());
        });

    // Print the currently-focused tab's chart.  The print dialog and
    // the actual painting live in song_widget::print; we just hand it
    // a fresh QPrinter targeting the visible tab.  Standard Ctrl+P
    // shortcut (QKeySequence::Print maps to the platform convention).
    // Guarded against having no open tab both here (defensive, for the
    // shortcut path) and via aboutToShow gating below.
    nashville_win_.actionPrint->setShortcut(QKeySequence::Print);
    QObject::connect(nashville_win_.actionPrint, &QAction::triggered,
        [this]() {
            if (auto* tab = main_window_->current_tab())
            {
                QPrinter printer;
                tab->widget()->print(&printer);
            }
        });

    // Bars per line: a song-level layout preference that reflows the
    // chart.  Targets current_body() because the reflow rewrites the
    // chart's per-bar line-break flags.  Gated on an open tab below.
    QObject::connect(nashville_win_.actionBarsPerLine, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->prompt_bars_per_line(); });

    // Info: swap the focused tab's content area to the metadata Info page.
    // Targets the song_tab (it owns the page swap), not current_body().
    QObject::connect(nashville_win_.actionInfo, &QAction::triggered,
        [this]() { if (auto* tab = main_window_->current_tab()) tab->show_info(); });

    QObject::connect(nashville_win_.actionInsertTextBox, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->set_annotation_tool(tool::text_box); });
    QObject::connect(nashville_win_.actionInsertLine, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->set_annotation_tool(tool::line); });
    QObject::connect(nashville_win_.actionInsertArrow, &QAction::triggered,
        [this]() { if (auto* b = current_body()) b->set_annotation_tool(tool::arrow); });

    // Gate the items that need a target.  New is always enabled (you
    // can create a song with no tabs open).  Delete needs an open
    // tab.  Insert items need an open tab (they target current_body).
    QObject::connect(nashville_win_.menuSong, &QMenu::aboutToShow,
        [this]() {
            const bool has_tab = current_body() != nullptr;
            nashville_win_.actionDeleteSong   ->setEnabled(has_tab);
            nashville_win_.actionPrint        ->setEnabled(has_tab);
            nashville_win_.actionBarsPerLine  ->setEnabled(has_tab);
            nashville_win_.actionInfo         ->setEnabled(has_tab);
            nashville_win_.actionInsertTextBox->setEnabled(has_tab);
            nashville_win_.actionInsertLine   ->setEnabled(has_tab);
            nashville_win_.actionInsertArrow  ->setEnabled(has_tab);
        });
}

// ---------------------------------------------------------------------------
// wire_playlist_menu
// ---------------------------------------------------------------------------
// Two actions: New and Delete.  New routes through main_window's
// prompt; Delete targets whatever's selected in the side panel's
// playlists list (playlists don't have tabs, so the side panel is
// the only cursor that makes sense).  Delete is disabled when no
// playlist is selected — the gating is done lazily on aboutToShow,
// same idiom as the other menus.
void app::wire_playlist_menu()
{
    QObject::connect(nashville_win_.actionNewPlaylist, &QAction::triggered,
        [this]() { main_window_->prompt_new_playlist(); });
    QObject::connect(nashville_win_.actionDeletePlaylist, &QAction::triggered,
        [this]() {
            const std::string name = main_window_->selected_playlist_name();
            if (!name.empty())
                main_window_->confirm_delete_playlist(name);
        });

    QObject::connect(nashville_win_.menuPlaylist, &QMenu::aboutToShow,
        [this]() {
            const bool has_sel = !main_window_->selected_playlist_name().empty();
            nashville_win_.actionDeletePlaylist->setEnabled(has_sel);
        });
}

// ---------------------------------------------------------------------------
// wire_view_menu
// ---------------------------------------------------------------------------
// The View menu's "Text size" submenu offers a few named presets that map
// to chart font-scale multipliers.  Picking one persists the scale (so new
// tabs and future sessions inherit it) and immediately re-lays-out every
// open chart.  The presets are an exclusive checkable group; their
// checkmark is re-synced from the persisted value on each menu open, so it
// stays correct even though nothing else in the app changes the scale.
//
// Keep the (action, scale) table below in sync with the actions declared
// in the .ui's Text size submenu.
void app::wire_view_menu()
{
    struct preset { QAction* action; double scale; };
    const std::vector<preset> presets = {
        { nashville_win_.actionTextNormal, 1.00 },
        { nashville_win_.actionTextLarge,  1.25 },
        { nashville_win_.actionTextXLarge, 1.50 },
        { nashville_win_.actionTextHuge,   2.00 },
    };

    // Exclusive group so exactly one size carries a checkmark.  Parented to
    // the main window so it lives for the app's lifetime.
    auto* group = new QActionGroup(&main_win_);
    group->setExclusive(true);
    for (const auto& p : presets)
        group->addAction(p.action);

    for (const auto& p : presets)
    {
        QObject::connect(p.action, &QAction::triggered,
            [this, scale = p.scale]() {
                ui_settings::set_font_scale(scale);
                main_window_->apply_font_scale_to_all_tabs(scale);
            });
    }

    // Reflect the persisted scale in the checkmarks each time the submenu
    // opens.  If the stored value matches no preset (shouldn't happen, since
    // only these presets ever write it), all items simply show unchecked.
    QObject::connect(nashville_win_.menuTextSize, &QMenu::aboutToShow,
        [this, presets]() {
            const double current = ui_settings::font_scale();
            for (const auto& p : presets)
                p.action->setChecked(std::abs(p.scale - current) < 1e-6);
        });

    // Page Setup: opens a modal dialog that edits the app-wide page size /
    // margins / display unit (all persisted via ui_settings).  On accept,
    // the chosen geometry is pushed to every open tab so their previews
    // re-paginate immediately; tabs opened later read the persisted values
    // themselves on construction.
    QObject::connect(nashville_win_.actionPageSetup, &QAction::triggered,
        [this]() {
            view::page_setup_dialog dlg(&main_win_);
            if (dlg.exec() == QDialog::Accepted)
                main_window_->apply_page_geometry_to_all_tabs(dlg.geometry());
        });
}

}
