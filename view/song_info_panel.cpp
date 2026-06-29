#include "song_info_panel.hpp"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QDateEdit>
#include <QAbstractSpinBox>
#include <QToolButton>
#include <QPushButton>
#include <QPalette>
#include <QDateTime>
#include <QDate>
#include <QFrame>

#include <chrono>

namespace nashville::view
{

namespace
{
constexpr int k_field_gap   = 10;   // px between fields
constexpr int k_page_margin = 20;

// Tag a label as a secondary "caption": the panel stylesheet dims these to
// a fixed medium grey that reads on white.  A dynamic property (not a leaf
// stylesheet) keeps the styling under the panel's single stylesheet, which
// is deterministic under QStyleSheetStyle.
void dim_label(QLabel* l)
{
    l->setProperty("caption", true);
}

// --- chrono <-> Qt conversions ---------------------------------------------
QString fmt_timestamp(std::chrono::sys_time<std::chrono::milliseconds> t)
{
    const qint64 ms = static_cast<qint64>(t.time_since_epoch().count());
    return QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd  HH:mm");
}

QDate to_qdate(std::chrono::sys_days sd)
{
    const std::chrono::year_month_day ymd{sd};
    return QDate(static_cast<int>(ymd.year()),
                 static_cast<int>(static_cast<unsigned>(ymd.month())),
                 static_cast<int>(static_cast<unsigned>(ymd.day())));
}

std::chrono::sys_days from_qdate(const QDate& d)
{
    using namespace std::chrono;
    return sys_days{year{d.year()} /
                    month(static_cast<unsigned>(d.month())) /
                    day(static_cast<unsigned>(d.day()))};
}

// Remove and delete every item (and its widget) from a layout so the view
// page can be rebuilt from scratch.  Deletion is immediate (not deferred):
// refresh_view() rebuilds synchronously and is never called from within one
// of these widgets' own event handlers, so deleting now is safe — and it
// avoids deleteLater() leaving the previous generation of labels alive and
// painting on top of the freshly-built ones until the event loop catches up.
void clear_layout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
}

// Append a "caption + value" pair to the read-only view, skipping empty
// values entirely (Contacts-style: absent fields aren't shown).
void add_view_field(QVBoxLayout* form, const QString& caption, const QString& value)
{
    if (value.trimmed().isEmpty())
        return;

    auto* cap = new QLabel(caption);
    dim_label(cap);

    auto* val = new QLabel(value);
    val->setWordWrap(true);
    val->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addWidget(cap);
    form->addWidget(val);
    form->addSpacing(k_field_gap);
}

// Build a labelled input row into a form layout.
void add_edit_field(QVBoxLayout* form, const QString& caption, QWidget* editor)
{
    auto* cap = new QLabel(caption);
    dim_label(cap);
    form->addWidget(cap);
    form->addWidget(editor);
    form->addSpacing(k_field_gap);
}

QString join_authors(const std::vector<std::string>& authors)
{
    QStringList parts;
    parts.reserve(static_cast<int>(authors.size()));
    for (const auto& a : authors)
        parts << QString::fromStdString(a);
    return parts.join(", ");
}
} // namespace

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------
song_info_panel::song_info_panel(model::song& song, QWidget* parent)
    : QWidget(parent)
    , song_(song)
{
    // Force the app's white "paper" look with black text.  The desktop theme
    // is dark, and an ancestor (the tab widget) already sets a stylesheet,
    // which puts this whole subtree under QStyleSheetStyle — that defeats
    // palette / autoFillBackground theming (which is why an earlier
    // palette-based attempt rendered as a black slab in the real app even
    // though it worked in isolation).  A stylesheet set on the panel itself
    // is the reliable override.  Captions are tagged with a dynamic
    // "caption" property and dimmed via the rule below; the separator and
    // inputs are styled explicitly so they read correctly on white (a styled
    // QFrame line, and bordered inputs that would otherwise be white-on-white).
    setStyleSheet(
        "QWidget { background-color: #ffffff; color: #000000; }"
        "QLabel[caption=\"true\"] { color: #666666; }"
        "QFrame#infoSep { background: none; border: none;"
        "                 border-top: 1px solid #dddddd; }"
        "QLineEdit, QPlainTextEdit, QDateEdit {"
        "    background-color: #ffffff; color: #000000;"
        "    border: 1px solid #c8c8c8; border-radius: 3px; padding: 3px;"
        "}"
        "QPushButton {"
        "    background-color: #f4f4f4; color: #000000;"
        "    border: 1px solid #c8c8c8; border-radius: 4px; padding: 4px 14px;"
        "}"
        "QPushButton:hover { background-color: #e9e9e9; }"
        "QToolButton { background: transparent; border: none; color: #000000; }"
        "QToolButton:hover { background-color: #f0f0f0; border-radius: 4px; }"
    );

    pages_ = new QStackedWidget(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(pages_);

    build_view_page();   // index 0
    build_edit_page();   // index 1

    show_view_mode();
}

// ---------------------------------------------------------------------------
// view page
// ---------------------------------------------------------------------------
void song_info_panel::build_view_page()
{
    auto* page  = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(k_page_margin, k_page_margin, k_page_margin, k_page_margin);

    // Header: [‹ Chart] ............................................. [✎]
    // No title text — the context (a song's Info page) is already clear.
    auto* header = new QHBoxLayout;
    auto* back = new QPushButton(tr("\u2039  Chart"));
    back->setFlat(true);
    connect(back, &QPushButton::clicked, this, [this]() { emit closed(); });

    auto* pen = new QToolButton;
    pen->setObjectName("penButton");
    pen->setText(QString::fromUtf8("\u270E"));   // pencil
    pen->setToolTip(tr("Edit"));
    pen->setAutoRaise(true);
    // Make the pencil a prominent, self-explanatory affordance — large
    // enough that it needs no on-screen "how to use it" caption.
    {
        QFont pf = pen->font();
        pf.setPointSizeF(pf.pointSizeF() + 14.0);
        pen->setFont(pf);
    }
    pen->setCursor(Qt::PointingHandCursor);
    connect(pen, &QToolButton::clicked, this, [this]() { enter_edit_mode(); });

    header->addWidget(back);
    header->addStretch(1);
    header->addWidget(pen);
    outer->addLayout(header);
    outer->addSpacing(8);

    // Scrollable body — guarantees long content never overflows the page.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    view_form_ = new QVBoxLayout(content);
    view_form_->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    pages_->addWidget(page);   // index 0
}

void song_info_panel::refresh_view()
{
    if (!view_form_)
        return;

    clear_layout(view_form_);

    const model::song::metadata& m = song_.meta();

    const QString authors = join_authors(m.authors);

    add_view_field(view_form_, tr("Authors"),   authors);
    add_view_field(view_form_, tr("Performer"), QString::fromStdString(m.original_performer));
    add_view_field(view_form_, tr("Album"),     QString::fromStdString(m.original_album));
    if (m.original_album_release_date)
        add_view_field(view_form_, tr("Release date"),
                       to_qdate(*m.original_album_release_date).toString("yyyy-MM-dd"));
    add_view_field(view_form_, tr("Notes"),     QString::fromStdString(m.notes));

    // Read-only timestamps, always shown, separated from the editable fields.
    // When there is no metadata yet the page is intentionally near-empty —
    // the (prominent) pencil is the affordance; no on-screen instructions.
    auto* rule = new QFrame;
    rule->setObjectName("infoSep");
    rule->setFrameShape(QFrame::HLine);
    rule->setFixedHeight(1);
    view_form_->addWidget(rule);
    view_form_->addSpacing(k_field_gap);

    auto* created = new QLabel(tr("Created   %1").arg(fmt_timestamp(m.creation_time)));
    dim_label(created);
    auto* modified = new QLabel(tr("Modified  %1").arg(fmt_timestamp(m.modification_time)));
    dim_label(modified);
    view_form_->addWidget(created);
    view_form_->addWidget(modified);

    view_form_->addStretch(1);
}

void song_info_panel::show_view_mode()
{
    refresh_view();
    pages_->setCurrentIndex(0);
}

// ---------------------------------------------------------------------------
// edit page
// ---------------------------------------------------------------------------
void song_info_panel::build_edit_page()
{
    auto* page  = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(k_page_margin, k_page_margin, k_page_margin, k_page_margin);

    // Header: [Cancel] ............................................. [Save]
    auto* header = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setFlat(true);
    connect(cancel, &QPushButton::clicked, this, [this]() { cancel_edit(); });

    auto* save = new QPushButton(tr("Save"));
    save->setObjectName("saveButton");
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, [this]() { save_edit(); });

    header->addWidget(cancel);
    header->addStretch(1);
    header->addWidget(save);
    outer->addLayout(header);
    outer->addSpacing(8);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* form = new QVBoxLayout(content);
    form->setContentsMargins(0, 0, 0, 0);

    authors_edit_ = new QLineEdit;
    authors_edit_->setObjectName("authorsEdit");
    authors_edit_->setPlaceholderText(tr("Separate multiple authors with commas"));
    add_edit_field(form, tr("Authors"), authors_edit_);

    performer_edit_ = new QLineEdit;
    performer_edit_->setObjectName("performerEdit");
    add_edit_field(form, tr("Performer"), performer_edit_);

    album_edit_ = new QLineEdit;
    album_edit_->setObjectName("albumEdit");
    add_edit_field(form, tr("Album"), album_edit_);

    // Release date: a "has date" checkbox gates an inline (no-popup) date
    // editor, so the field can be left unset and nothing pops a window.
    has_date_  = new QCheckBox(tr("Has release date"));
    has_date_->setObjectName("hasDate");
    date_edit_ = new QDateEdit;
    date_edit_->setObjectName("dateEdit");
    date_edit_->setDisplayFormat("yyyy-MM-dd");
    date_edit_->setCalendarPopup(false);   // inline editor — Wayland-safe
    // Drop the up/down spin buttons.  Under the panel stylesheet they render
    // as empty squares (Qt only paints custom spin arrows from an image
    // resource, which we don't ship).  The date stays fully editable: click a
    // segment and type, or use the keyboard up/down arrows.
    date_edit_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(has_date_, &QCheckBox::toggled, date_edit_, &QWidget::setEnabled);
    auto* date_row = new QHBoxLayout;
    date_row->addWidget(has_date_);
    date_row->addWidget(date_edit_);
    date_row->addStretch(1);
    auto* date_cap = new QLabel(tr("Release date"));
    dim_label(date_cap);
    form->addWidget(date_cap);
    form->addLayout(date_row);
    form->addSpacing(k_field_gap);

    notes_edit_ = new QPlainTextEdit;
    notes_edit_->setObjectName("notesEdit");
    notes_edit_->setMinimumHeight(120);
    add_edit_field(form, tr("Notes"), notes_edit_);

    form->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    pages_->addWidget(page);   // index 1
}

void song_info_panel::populate_edit_from_model()
{
    const model::song::metadata& m = song_.meta();

    authors_edit_->setText(join_authors(m.authors));
    performer_edit_->setText(QString::fromStdString(m.original_performer));
    album_edit_->setText(QString::fromStdString(m.original_album));

    if (m.original_album_release_date)
    {
        has_date_->setChecked(true);
        date_edit_->setDate(to_qdate(*m.original_album_release_date));
    }
    else
    {
        has_date_->setChecked(false);
        date_edit_->setDate(QDate::currentDate());
    }
    date_edit_->setEnabled(has_date_->isChecked());

    notes_edit_->setPlainText(QString::fromStdString(m.notes));
}

void song_info_panel::apply_edit_to_model()
{
    model::song::metadata& m = song_.meta();

    // Authors: split on commas, trim, drop empties.
    std::vector<std::string> authors;
    const QStringList parts =
        authors_edit_->text().split(',', Qt::SkipEmptyParts);
    for (const QString& p : parts)
    {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            authors.push_back(t.toStdString());
    }
    m.authors = std::move(authors);

    m.original_performer = performer_edit_->text().trimmed().toStdString();
    m.original_album     = album_edit_->text().trimmed().toStdString();
    m.notes              = notes_edit_->toPlainText().toStdString();

    if (has_date_->isChecked())
        m.original_album_release_date = from_qdate(date_edit_->date());
    else
        m.original_album_release_date.reset();
}

// ---------------------------------------------------------------------------
// mode transitions
// ---------------------------------------------------------------------------
void song_info_panel::enter_edit_mode()
{
    populate_edit_from_model();
    pages_->setCurrentIndex(1);
}

void song_info_panel::save_edit()
{
    apply_edit_to_model();
    emit committed();      // host schedules the persist + modification_time bump
    show_view_mode();
}

void song_info_panel::cancel_edit()
{
    show_view_mode();      // discard: edit inputs are repopulated on next entry
}

} // namespace nashville::view
