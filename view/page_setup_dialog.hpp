#pragma once

// Modal "Page Setup" dialog: edits the app-wide page size, per-side margins,
// and the margin display unit.  Reads current values from ui_settings on
// open and, on Accept, writes them back and hands the resulting
// page_geometry to its caller (app::wire_view_menu) to push to open tabs.
//
// The dialog is intentionally self-contained — it owns no application state
// beyond the working copy it edits — so it can be constructed on demand,
// exec()'d, and discarded.

#include "../page_geometry.hpp"
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

namespace nashville::view
{

class page_setup_dialog : public QDialog
{
    Q_OBJECT

public:
    explicit page_setup_dialog(QWidget* parent = nullptr);

    // The page geometry (size + margins, in points) reflecting the dialog's
    // current control values.  Meaningful after exec() returns Accepted.
    page_geometry geometry() const;

private:
    // Re-label the margin spin boxes' suffix and rescale their displayed
    // values when the unit selector changes, without touching the
    // underlying point values.  Also re-clamps the spin ranges to the
    // ui_settings margin min/max expressed in the new unit.
    void apply_unit_to_controls(nashville::length_unit unit);

    // The four margin spin boxes always *display* in the currently-selected
    // unit; these convert between that display value and the canonical
    // points we store/return.
    qreal spin_to_points(const QDoubleSpinBox* spin) const;
    void  set_spin_from_points(QDoubleSpinBox* spin, qreal points);

    QComboBox*      size_combo_   = nullptr;
    QComboBox*      unit_combo_   = nullptr;
    QDoubleSpinBox* top_spin_     = nullptr;
    QDoubleSpinBox* bottom_spin_  = nullptr;
    QDoubleSpinBox* left_spin_    = nullptr;
    QDoubleSpinBox* right_spin_   = nullptr;

    // The unit the spin boxes currently display in.  Tracked so a unit
    // change can convert the existing point values into the new unit's
    // display scale rather than reinterpreting the raw numbers.
    nashville::length_unit current_unit_ = nashville::length_unit::inches;
};

} // namespace nashville::view
