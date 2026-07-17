#include "font_size_dialog.hpp"
#include "../ui_settings.hpp"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QGroupBox>

namespace nashville::view
{

namespace
{
// Percentages are friendlier than raw multipliers in a size dialog: users
// think "120%", not "1.2".  5% steps give fine control without fiddliness.
QDoubleSpinBox* make_percent_spin(QWidget* parent)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setSuffix(QStringLiteral(" %"));
    s->setDecimals(0);
    s->setSingleStep(5.0);
    s->setAlignment(Qt::AlignRight);
    s->setRange(ui_settings::HEADER_SCALE_MIN * 100.0,
                ui_settings::HEADER_SCALE_MAX * 100.0);
    return s;
}
} // namespace

font_size_dialog::font_size_dialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Title & Margin Size"));
    setModal(true);

    title_spin_  = make_percent_spin(this);
    margin_spin_ = make_percent_spin(this);

    // Seed from the persisted scales (stored as multipliers).
    title_spin_->setValue(ui_settings::title_scale()  * 100.0);
    margin_spin_->setValue(ui_settings::margin_scale() * 100.0);

    auto* form = new QFormLayout;
    form->addRow(tr("Song title:"),               title_spin_);
    form->addRow(tr("Margin (key / time / tempo):"), margin_spin_);

    auto* group = new QGroupBox(tr("Font sizes"), this);
    group->setLayout(form);

    // A quiet note that the chart body size lives elsewhere, so nobody hunts
    // for it in this dialog.
    auto* hint = new QLabel(
        tr("The chart (bar) size is set separately under View \u2192 Text size."),
        this);
    hint->setWordWrap(true);
    hint->setEnabled(false);  // renders muted

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addWidget(group);
    root->addWidget(hint);
    root->addWidget(buttons);
}

qreal font_size_dialog::title_scale() const
{
    return title_spin_->value() / 100.0;
}

qreal font_size_dialog::margin_scale() const
{
    return margin_spin_->value() / 100.0;
}

} // namespace nashville::view
