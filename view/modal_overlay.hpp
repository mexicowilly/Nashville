#pragma once

#include <QWidget>
#include <QString>
#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace nashville::view
{

// In-widget modal overlay.  A QWidget parented to its host (the
// main_window) that covers the entire client area with a dimming
// rect, and centers a "card" widget inside that holds the prompt
// UI.  Used in place of QDialog / QInputDialog / QMessageBox
// because:
//
//   * On Wayland, applications cannot set the absolute screen
//     position of their own toplevel windows — the xdg-shell
//     protocol doesn't include a "place here" message.  Every
//     attempt to position a real dialog window is silently ignored
//     by the compositor.  An in-widget overlay sidesteps the
//     entire problem because it isn't a separate window; it's
//     drawn into the existing main window's surface, with positions
//     computed in the parent's local coordinates.
//
//   * The pattern is widely used in modern UIs (VS Code, Slack,
//     web apps) and feels native to anyone familiar with those.
//
//   * No window manager involvement means consistent behavior
//     across X11, Wayland, Windows, and macOS — the overlay
//     appears exactly where we draw it, every time.
//
// The overlay handles event blocking automatically: while it's
// visible, mouse and keyboard events directed at the rest of the
// main window are intercepted and dropped (clicks on the dim
// background cancel, but don't fall through to the chart below).
//
// Three usage modes are offered, all returning their answer via
// callback (asynchronous-style; the overlay does not block the
// caller, which is a deliberate departure from QDialog::exec).
// Callbacks fire on the same thread that opened the overlay, after
// the event loop has had a chance to process the user's response.
class modal_overlay : public QWidget
{
    Q_OBJECT
public:
    explicit modal_overlay(QWidget* host);

    // Prompt the user for a single line of text.  Calls `on_done`
    // with the trimmed result on OK (including empty string when
    // allow_empty is true), or std::nullopt on Cancel / dismiss.
    // When allow_empty is false (the default), an empty-after-trim
    // result is treated as Cancel.  `initial_text` pre-populates
    // the line edit.  `select_all` selects the initial text so the
    // user can immediately replace it by typing.  `min_width` sets
    // a minimum card width (px) to prevent long labels from wrapping.
    void prompt_text(const QString& title,
                     const QString& label,
                     std::function<void(std::optional<QString>)> on_done,
                     const QString& initial_text = {},
                     bool allow_empty = false,
                     bool select_all = false,
                     int min_width = 0);

    // Yes/No confirmation.  `on_done` receives true iff the user
    // clicked Yes; clicking No, clicking the background, or
    // pressing Esc all yield false.  Default focus is on No, so an
    // accidental Enter doesn't confirm a destructive action.
    void confirm(const QString& title,
                 const QString& text,
                 std::function<void(bool)> on_done);

    // Informational popup with a single OK button.  No callback
    // because there's nothing to report — the user has either seen
    // it or not.  Optional `on_dismissed` for callers that want
    // to chain UI flow after the user closes it.
    void message(const QString& title,
                 const QString& text,
                 std::function<void()> on_dismissed = nullptr);

protected:
    // Paint the dimming layer over the host's chart.
    void paintEvent(QPaintEvent* event) override;

    // Resize to cover the host whenever the host resizes.  Watched
    // via eventFilter installed on host.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Keyboard handling at the overlay level — Esc cancels, Enter
    // accepts (when an accept button is visible and enabled).
    void keyPressEvent(QKeyEvent* event) override;

    // Click outside the card cancels.  Inside-card clicks are
    // handled by the card's own widgets.
    void mousePressEvent(QMouseEvent* event) override;

private:
    // Build (or rebuild) the card with the given content widgets.
    // The card itself is a fixed-style frame with a header label,
    // a body area populated by the caller, and a button row.
    // `accept_label` and `reject_label` set the button text;
    // either can be empty to omit that button entirely.
    struct card_config
    {
        QString title;
        QWidget* body_widget = nullptr;   // overlay takes ownership
        QString accept_label;             // empty = no button
        QString reject_label;             // empty = no button
        // Callbacks for each button.  Either may be null; if so,
        // the corresponding button does nothing beyond hiding the
        // overlay.
        std::function<void()> on_accept;
        std::function<void()> on_reject;
        // Optional default-focus target inside the body.  If null,
        // the accept button takes focus.
        QWidget* default_focus = nullptr;
        // When true, call selectAll() on default_focus after focusing it
        // (useful for pre-filled line edits where the user will likely
        // retype the whole value rather than amend it).
        bool select_all_on_focus = false;
        // Minimum card width in pixels.  0 means no override (card sizes
        // to its content up to the 420px cap).  Use when the label text
        // is long enough that the auto-sized card would wrap awkwardly.
        int min_width = 0;
    };
    void show_card(const card_config& cfg);

    // Hide the overlay and re-enable the host.  Called by every
    // dismiss path: accept, reject, Esc, click-outside.
    void dismiss();

    // Reposition the card in the center of the overlay.  Called on
    // overlay resize and when the card's size changes.
    void recenter_card();

    QWidget* host_ = nullptr;

    // The card frame.  Constructed lazily on the first prompt;
    // reused for subsequent prompts to keep widget construction
    // costs low.  The card's content (label + input + buttons) is
    // torn down and rebuilt for each new prompt via show_card.
    QWidget*     card_         = nullptr;
    QVBoxLayout* card_layout_  = nullptr;
    QLabel*      title_label_  = nullptr;
    QWidget*     body_holder_  = nullptr;  // host for the body_widget
    QPushButton* accept_btn_   = nullptr;
    QPushButton* reject_btn_   = nullptr;

    // The "on_accept" / "on_reject" closures for the current
    // prompt.  Cleared after invocation so a stray second click
    // (which shouldn't happen but defensively) doesn't double-fire.
    std::function<void()> current_on_accept_;
    std::function<void()> current_on_reject_;
};

} // namespace nashville::view
