#pragma once

// Modal "Title & Margin Size" dialog: edits the two app-wide font scales
// that sit outside the chart body's automatic sizing — the song title and
// the left-margin gutter (key / time signature / tempo).  Reads the current
// values from ui_settings on open and, on Accept, exposes the chosen scales
// to its caller (app::wire_view_menu) to persist and push to open tabs.
//
// Deliberately self-contained (mirrors page_setup_dialog): it owns no
// application state beyond the working values it edits, so it can be
// constructed on demand, exec()'d, and discarded.  The chart-body font size
// keeps its own control (View > Text size) and is not touched here.

#include <QDialog>

class QDoubleSpinBox;

namespace nashville::view
{

class font_size_dialog : public QDialog
{
    Q_OBJECT

public:
    explicit font_size_dialog(QWidget* parent = nullptr);

    // The chosen scales (1.0 == shipped size).  Meaningful after exec()
    // returns Accepted.  Values are the spin-box percentages divided by 100.
    qreal title_scale() const;
    qreal margin_scale() const;

private:
    QDoubleSpinBox* title_spin_  = nullptr;
    QDoubleSpinBox* margin_spin_ = nullptr;
};

} // namespace nashville::view
