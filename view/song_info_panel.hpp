#pragma once

#include "../model/song.hpp"
#include <QWidget>

class QStackedWidget;
class QVBoxLayout;
class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
class QDateEdit;

namespace nashville::view
{

// Full-page "Info" view for a song's descriptive metadata, modelled on the
// Google-Contacts pattern: a read-only VIEW of the metadata with a pencil
// affordance that flips to an EDIT form, plus Save / Cancel.  It is a plain
// child widget (no QDialog, no modal_overlay) so it renders correctly under
// Wayland and can never cramp its contents — everything lives inside a
// QScrollArea, so long notes or many authors scroll instead of overflowing.
//
// Scope is deliberately ONLY the metadata struct: authors, original
// performer, original album, release date and notes (editable), plus the
// created / modified timestamps (read-only).  Title, key, tempo, time
// signature and the rest of the chart are edited in the chart itself and are
// intentionally not part of this transaction.
//
// Ownership / hosting: song_tab keeps one of these alongside the chart page
// in a QStackedWidget and swaps to it when the "Song > Info..." action
// fires.  The panel mutates song.meta() directly on Save and emits
// committed(); song_tab responds by kicking its debounced autosave, which
// persists the change and advances modification_time (the save path's
// same_content_as comparison already covers every metadata field).  The
// panel never persists anything itself.
class song_info_panel : public QWidget
{
    Q_OBJECT
public:
    explicit song_info_panel(model::song& song, QWidget* parent = nullptr);

    // Reset to read-only VIEW mode and rebuild it from the current model.
    // song_tab calls this every time it shows the panel, so the panel always
    // opens in view mode showing fresh data (and never lands the user in a
    // half-finished edit from a previous visit).
    void show_view_mode();

    // Rebuild the read-only view from the model without changing which mode
    // is shown.  Cheap and safe to call anytime; song_tab calls it after a
    // save so the displayed "Modified" timestamp stays current.
    void refresh_view();

signals:
    // Save was pressed: the model's metadata has been updated in place and
    // the host should schedule a persist.  (Emitted unconditionally; the
    // save path decides whether anything actually changed.)
    void committed();

    // The user asked to leave the info page and return to the chart.
    void closed();

private:
    void build_view_page();
    void build_edit_page();

    void enter_edit_mode();
    void save_edit();
    void cancel_edit();

    void populate_edit_from_model();
    void apply_edit_to_model();

    model::song&    song_;

    QStackedWidget* pages_      = nullptr;  // 0 = view, 1 = edit

    // View page: a vertical layout we clear and rebuild on every refresh.
    QVBoxLayout*    view_form_  = nullptr;

    // Edit page inputs.
    QLineEdit*      authors_edit_   = nullptr;  // comma-separated
    QLineEdit*      performer_edit_ = nullptr;
    QLineEdit*      album_edit_     = nullptr;
    QCheckBox*      has_date_       = nullptr;
    QDateEdit*      date_edit_      = nullptr;
    QPlainTextEdit* notes_edit_     = nullptr;
};

} // namespace nashville::view
