#include "page_setup_dialog.hpp"
#include "../ui_settings.hpp"

#include <QComboBox>
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
// How many decimal places / what step the spin boxes use per unit.  Inches
// want finer granularity (0.05in ~= 3.6pt) than centimeters (0.1cm = 1mm),
// matching what the respective unit's users expect to dial in.
struct unit_display { const char* suffix; int decimals; double step; };

unit_display display_for(nashville::length_unit u)
{
    return u == nashville::length_unit::inches
        ? unit_display{ " in", 2, 0.05 }
        : unit_display{ " cm", 1, 0.1 };
}
} // namespace

page_setup_dialog::page_setup_dialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Page Setup"));
    setModal(true);

    // --- Page size ---
    size_combo_ = new QComboBox(this);
    size_combo_->addItem(tr("US Letter (8.5 x 11 in)"),
                         static_cast<int>(nashville::page_size::letter));
    size_combo_->addItem(tr("A4 (210 x 297 mm)"),
                         static_cast<int>(nashville::page_size::a4));
    {
        const auto cur = ui_settings::page_size();
        size_combo_->setCurrentIndex(
            size_combo_->findData(static_cast<int>(cur)));
    }

    // --- Units ---
    // The combo offers Auto plus the two explicit overrides.  Auto is
    // stored as a preference; the spin boxes themselves always display in a
    // concrete unit, so we resolve Auto to a concrete unit for display
    // while remembering that the *preference* is Auto when we save.
    unit_combo_ = new QComboBox(this);
    unit_combo_->addItem(tr("Auto (from system locale)"),
                         static_cast<int>(ui_settings::length_unit_pref::auto_detect));
    unit_combo_->addItem(tr("Inches"),
                         static_cast<int>(ui_settings::length_unit_pref::inches));
    unit_combo_->addItem(tr("Centimeters"),
                         static_cast<int>(ui_settings::length_unit_pref::centimeters));
    unit_combo_->setCurrentIndex(
        unit_combo_->findData(static_cast<int>(ui_settings::length_unit_preference())));

    // --- Margin spin boxes ---
    auto make_spin = [this]() {
        auto* s = new QDoubleSpinBox(this);
        s->setAlignment(Qt::AlignRight);
        return s;
    };
    top_spin_    = make_spin();
    bottom_spin_ = make_spin();
    left_spin_   = make_spin();
    right_spin_  = make_spin();

    // Seed the spin boxes from the persisted margins.  We resolve the unit
    // preference to a concrete display unit first, then push the stored
    // point values in.
    current_unit_ = ui_settings::effective_length_unit();
    apply_unit_to_controls(current_unit_);
    {
        const auto m = ui_settings::margins();
        set_spin_from_points(top_spin_,    m.top);
        set_spin_from_points(bottom_spin_, m.bottom);
        set_spin_from_points(left_spin_,   m.left);
        set_spin_from_points(right_spin_,  m.right);
    }

    // Changing the unit re-labels and rescales the spin boxes in place,
    // preserving the physical margin each represents.
    connect(unit_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int) {
            const auto pref = static_cast<ui_settings::length_unit_pref>(
                unit_combo_->currentData().toInt());
            nashville::length_unit concrete =
                pref == ui_settings::length_unit_pref::centimeters
                    ? nashville::length_unit::centimeters
                : pref == ui_settings::length_unit_pref::inches
                    ? nashville::length_unit::inches
                    // Auto: resolve via locale for display purposes.
                    : ui_settings::effective_length_unit();

            // Capture current physical values (points) before rescaling,
            // then re-seed after the range/suffix change so the same
            // physical margins show in the new unit.
            const qreal t = spin_to_points(top_spin_);
            const qreal b = spin_to_points(bottom_spin_);
            const qreal l = spin_to_points(left_spin_);
            const qreal r = spin_to_points(right_spin_);
            current_unit_ = concrete;
            apply_unit_to_controls(concrete);
            set_spin_from_points(top_spin_,    t);
            set_spin_from_points(bottom_spin_, b);
            set_spin_from_points(left_spin_,   l);
            set_spin_from_points(right_spin_,  r);
        });

    // --- Layout ---
    auto* form = new QFormLayout;
    form->addRow(tr("Page size:"), size_combo_);
    form->addRow(tr("Units:"),     unit_combo_);

    auto* margin_box = new QGroupBox(tr("Margins"), this);
    auto* mform = new QFormLayout(margin_box);
    mform->addRow(tr("Top:"),    top_spin_);
    mform->addRow(tr("Bottom:"), bottom_spin_);
    mform->addRow(tr("Left:"),   left_spin_);
    mform->addRow(tr("Right:"),  right_spin_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        // Persist everything on accept.  The unit *preference* (which may
        // be Auto) is saved as chosen, distinct from the concrete unit the
        // spin boxes happened to display in.
        ui_settings::set_page_size(
            static_cast<nashville::page_size>(size_combo_->currentData().toInt()));
        ui_settings::set_length_unit_preference(
            static_cast<ui_settings::length_unit_pref>(unit_combo_->currentData().toInt()));
        nashville::page_margins m;
        m.top    = spin_to_points(top_spin_);
        m.bottom = spin_to_points(bottom_spin_);
        m.left   = spin_to_points(left_spin_);
        m.right  = spin_to_points(right_spin_);
        ui_settings::set_margins(m);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(margin_box);
    root->addWidget(buttons);
}

void page_setup_dialog::apply_unit_to_controls(nashville::length_unit unit)
{
    const auto disp = display_for(unit);
    // Spin range = ui_settings margin clamp expressed in this unit.
    const double lo = nashville::from_points(ui_settings::MARGIN_MIN_PT, unit);
    const double hi = nashville::from_points(ui_settings::MARGIN_MAX_PT, unit);
    for (QDoubleSpinBox* s : { top_spin_, bottom_spin_, left_spin_, right_spin_ })
    {
        s->setSuffix(disp.suffix);
        s->setDecimals(disp.decimals);
        s->setSingleStep(disp.step);
        s->setRange(lo, hi);
    }
}

qreal page_setup_dialog::spin_to_points(const QDoubleSpinBox* spin) const
{
    return nashville::to_points(spin->value(), current_unit_);
}

void page_setup_dialog::set_spin_from_points(QDoubleSpinBox* spin, qreal points)
{
    spin->setValue(nashville::from_points(points, current_unit_));
}

page_geometry page_setup_dialog::geometry() const
{
    page_geometry geo;
    geo.size = page_size_points(
        static_cast<nashville::page_size>(size_combo_->currentData().toInt()));
    geo.margins.top    = spin_to_points(top_spin_);
    geo.margins.bottom = spin_to_points(bottom_spin_);
    geo.margins.left   = spin_to_points(left_spin_);
    geo.margins.right  = spin_to_points(right_spin_);
    return geo;
}

} // namespace nashville::view
