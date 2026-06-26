#include "modal_overlay.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>

namespace nashville::view
{

modal_overlay::modal_overlay(QWidget* host)
    : QWidget(host)
    , host_(host)
{
    // The overlay sits on top of every sibling widget and intercepts
    // all input.  Initially hidden — show_card displays it.
    hide();
    // Auto-fill background so paintEvent's fillRect against a
    // semi-transparent black has something to compose against.
    // Without this, on some platforms the area underneath shows
    // through with no dimming.
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, false);

    // Track host resizes so we always cover the entire client area.
    // The overlay is parented to host, so it moves with it
    // automatically, but its size needs to follow.
    host_->installEventFilter(this);
    setGeometry(host_->rect());

    // Build the card frame once — its content (body widget,
    // buttons) is swapped in show_card.  Card uses a stylesheet so
    // it visually reads as a popover regardless of the desktop
    // theme: white background, light shadow via a thin border,
    // rounded corners.  Same print-style palette as everywhere
    // else in the app.
    card_ = new QWidget(this);
    card_->setObjectName("modal_card");
    card_->setStyleSheet(
        "#modal_card {"
        "  background-color: white;"
        "  border: 1px solid #999;"
        "  border-radius: 8px;"
        "}"
        "#modal_card QLabel {"
        "  color: black;"
        "}"
        "#modal_card QLineEdit {"
        "  background-color: white;"
        "  color: black;"
        "  border: 1px solid #999;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "}"
        "#modal_card QPushButton {"
        "  background-color: #f0f0f0;"
        "  color: black;"
        "  border: 1px solid #999;"
        "  border-radius: 4px;"
        "  padding: 4px 12px;"
        "  min-width: 60px;"
        "}"
        "#modal_card QPushButton:default {"
        "  background-color: #d6e9ff;"
        "  border-color: #6699cc;"
        "}"
        "#modal_card QPushButton:hover {"
        "  background-color: #e8e8e8;"
        "}"
        "#modal_card QPushButton:default:hover {"
        "  background-color: #b8d6f5;"
        "}");
    card_layout_ = new QVBoxLayout(card_);
    card_layout_->setContentsMargins(16, 16, 16, 16);
    card_layout_->setSpacing(12);

    // Title.  Bold to read as a header against the body.
    title_label_ = new QLabel(card_);
    QFont tf = title_label_->font();
    tf.setBold(true);
    tf.setPointSize(tf.pointSize() + 1);
    title_label_->setFont(tf);
    title_label_->setWordWrap(true);
    card_layout_->addWidget(title_label_);

    // Body holder: a container for the caller's body widget.  We
    // hold it in a wrapper so we can swap the inner widget between
    // prompts without destroying the layout structure.
    body_holder_ = new QWidget(card_);
    auto* body_layout = new QVBoxLayout(body_holder_);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(8);
    card_layout_->addWidget(body_holder_);

    // Button row.  Right-aligned per platform convention.  The
    // accept button is initially marked as the default so Enter
    // confirms; show_card adjusts based on the card's config.
    auto* btn_row = new QHBoxLayout;
    btn_row->setSpacing(8);
    btn_row->addStretch(1);
    reject_btn_ = new QPushButton(card_);
    accept_btn_ = new QPushButton(card_);
    btn_row->addWidget(reject_btn_);
    btn_row->addWidget(accept_btn_);
    card_layout_->addLayout(btn_row);

    connect(accept_btn_, &QPushButton::clicked, this, [this]() {
        // Stash the closure locally before dismissing so dismiss()
        // clearing current_on_accept_ doesn't drop it before we
        // invoke it.  Same idiom in reject.
        auto cb = std::move(current_on_accept_);
        dismiss();
        if (cb) cb();
    });
    connect(reject_btn_, &QPushButton::clicked, this, [this]() {
        auto cb = std::move(current_on_reject_);
        dismiss();
        if (cb) cb();
    });

    card_->adjustSize();
}

void modal_overlay::prompt_text(const QString& title,
                                const QString& label,
                                std::function<void(std::optional<QString>)> on_done,
                                const QString& initial_text,
                                bool allow_empty,
                                bool select_all,
                                int min_width)
{
    // Build a body containing [label text on its own line] +
    // [QLineEdit].  The line edit is the default focus target so
    // the user can immediately start typing.  Enter triggers
    // accept via the QLineEdit::returnPressed -> accept_btn click
    // connection.
    auto* body = new QWidget;
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(6);

    auto* prompt_label = new QLabel(label, body);
    prompt_label->setWordWrap(true);
    bl->addWidget(prompt_label);

    auto* edit = new QLineEdit(body);
    if (!initial_text.isEmpty())
        edit->setText(initial_text);
    bl->addWidget(edit);

    // Enter in the text field = OK click.  Esc is handled at the
    // overlay level via keyPressEvent.
    connect(edit, &QLineEdit::returnPressed, accept_btn_, &QPushButton::click);

    card_config cfg;
    cfg.title         = title;
    cfg.body_widget   = body;
    cfg.accept_label  = tr("OK");
    cfg.reject_label  = tr("Cancel");
    cfg.default_focus      = edit;
    cfg.select_all_on_focus = select_all;
    cfg.min_width           = min_width;
    // Capture `edit` and `allow_empty` so we can read the text on accept.
    // Capture by raw pointer is safe because the overlay owns the body
    // widget hierarchy and won't destroy it until dismiss runs.
    cfg.on_accept = [edit, on_done, allow_empty]() {
        const QString trimmed = edit->text().trimmed();
        if (trimmed.isEmpty() && !allow_empty)
            on_done(std::nullopt);
        else
            on_done(trimmed);
    };
    cfg.on_reject = [on_done]() { on_done(std::nullopt); };
    show_card(cfg);
}

void modal_overlay::confirm(const QString& title,
                            const QString& text,
                            std::function<void(bool)> on_done)
{
    auto* body = new QWidget;
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    auto* msg = new QLabel(text, body);
    msg->setWordWrap(true);
    bl->addWidget(msg);

    card_config cfg;
    cfg.title        = title;
    cfg.body_widget  = body;
    cfg.accept_label = tr("Yes");
    cfg.reject_label = tr("No");
    // Default focus on the reject button: destructive operations
    // should make "no" the easy answer.  An accidental Enter on a
    // delete prompt won't lose data.
    cfg.default_focus = reject_btn_;
    cfg.on_accept = [on_done]() { on_done(true); };
    cfg.on_reject = [on_done]() { on_done(false); };
    show_card(cfg);
}

void modal_overlay::message(const QString& title,
                            const QString& text,
                            std::function<void()> on_dismissed)
{
    auto* body = new QWidget;
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    auto* msg = new QLabel(text, body);
    msg->setWordWrap(true);
    bl->addWidget(msg);

    card_config cfg;
    cfg.title        = title;
    cfg.body_widget  = body;
    cfg.accept_label = tr("OK");
    // No reject button — the empty string in card_config means
    // "omit this button entirely."  show_card hides it.
    cfg.reject_label = QString();
    cfg.on_accept = [on_dismissed]() { if (on_dismissed) on_dismissed(); };
    cfg.on_reject = [on_dismissed]() { if (on_dismissed) on_dismissed(); };
    show_card(cfg);
}

void modal_overlay::show_card(const card_config& cfg)
{
    title_label_->setText(cfg.title);

    // Tear out the previous body widget (if any) and install the
    // new one.  The old body's children include closures and Qt
    // signal connections; deleteLater queues their destruction to
    // the next event-loop turn, safely after the current click
    // handler returns.
    auto* body_layout = qobject_cast<QVBoxLayout*>(body_holder_->layout());
    if (body_layout)
    {
        while (auto* item = body_layout->takeAt(0))
        {
            if (auto* w = item->widget())
                w->deleteLater();
            delete item;
        }
        if (cfg.body_widget)
        {
            cfg.body_widget->setParent(body_holder_);
            body_layout->addWidget(cfg.body_widget);
        }
    }

    // Configure the buttons.  Empty label means "hide this button."
    if (cfg.accept_label.isEmpty())
    {
        accept_btn_->hide();
    }
    else
    {
        accept_btn_->show();
        accept_btn_->setText(cfg.accept_label);
        accept_btn_->setDefault(true);
    }
    if (cfg.reject_label.isEmpty())
    {
        reject_btn_->hide();
    }
    else
    {
        reject_btn_->show();
        reject_btn_->setText(cfg.reject_label);
        reject_btn_->setDefault(false);
    }
    current_on_accept_ = cfg.on_accept;
    current_on_reject_ = cfg.on_reject;

    // Size the card to fit its contents and center it in the
    // overlay.  adjustSize triggers a layout pass on the card,
    // after which recenter_card positions it.  Clamp the card's
    // width so the prompt doesn't stretch across a huge window —
    // 400px is a comfortable reading width for a one-line input.
    card_->adjustSize();
    int max_w = std::min(420, host_ ? host_->width() - 40 : 420);
    if (cfg.min_width > 0 && card_->width() < cfg.min_width)
    {
        card_->setFixedWidth(std::min(cfg.min_width, max_w));
        card_->adjustSize();
    }
    if (card_->width() > max_w)
    {
        card_->setFixedWidth(max_w);
        card_->adjustSize();
    }
    recenter_card();

    // Make sure the overlay is on top of everything in the host,
    // visible, and has focus.  raise() reorders within the parent's
    // child stack.  setFocus + the default-focus widget below gives
    // the user immediate keyboard interaction.
    setGeometry(host_->rect());
    raise();
    show();
    setFocus();
    if (cfg.default_focus)
    {
        cfg.default_focus->setFocus();
        if (cfg.select_all_on_focus)
            if (auto* le = qobject_cast<QLineEdit*>(cfg.default_focus))
                le->selectAll();
    }
    else
        accept_btn_->setFocus();
}

void modal_overlay::dismiss()
{
    // Drop the closures so a follow-up dismiss path (e.g. Esc after
    // accept) can't double-fire them.  hide() takes the overlay
    // out of the event-routing tree, releasing focus back to the
    // host.
    current_on_accept_ = nullptr;
    current_on_reject_ = nullptr;
    hide();
    if (host_)
        host_->setFocus();
}

void modal_overlay::recenter_card()
{
    if (!card_) return;
    const int x = (width()  - card_->width())  / 2;
    const int y = (height() - card_->height()) / 2;
    card_->move(x, y);
}

void modal_overlay::paintEvent(QPaintEvent* /*event*/)
{
    // Dim the host's contents behind the card.  Semi-transparent
    // black at ~40% gives a clear "modal mode" cue without making
    // the underlying chart illegible (the user might want to read
    // the chart to decide what to type).
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 100));
}

bool modal_overlay::eventFilter(QObject* watched, QEvent* event)
{
    // Mirror the host's size whenever it changes.  Without this,
    // resizing the main window would leave the overlay at its old
    // size, exposing the chart underneath.  We watch the host
    // directly (not its children) so this fires exactly once per
    // host resize.
    if (watched == host_ && event->type() == QEvent::Resize)
    {
        setGeometry(host_->rect());
        recenter_card();
    }
    return QWidget::eventFilter(watched, event);
}

void modal_overlay::keyPressEvent(QKeyEvent* event)
{
    // Esc cancels — invoke the reject closure if there is one,
    // otherwise just dismiss.  Enter is handled by the focused
    // widget's own returnPressed signal (text input) or by the
    // default button's keyboard activation (no input), so we don't
    // need to handle it here.
    if (event->key() == Qt::Key_Escape)
    {
        auto cb = std::move(current_on_reject_);
        dismiss();
        if (cb) cb();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void modal_overlay::mousePressEvent(QMouseEvent* event)
{
    // Clicks outside the card = cancel.  Inside-card clicks reach
    // the card's children directly (the overlay's mousePressEvent
    // only fires for clicks on the overlay itself, not on
    // descendants).  We test against the card's geometry to be
    // safe.
    if (card_ && card_->geometry().contains(event->pos()))
    {
        QWidget::mousePressEvent(event);
        return;
    }
    auto cb = std::move(current_on_reject_);
    dismiss();
    if (cb) cb();
    event->accept();
}

} // namespace nashville::view
