#include "title_text.hpp"

namespace nashville::widgets
{

title_text::title_text()
{
    setFrame(false);
    setPlaceholderText("Title");
    setAlignment(Qt::AlignHCenter);
    setReadOnly(true);
    connect(this, &title_text::editingFinished,
            this, &title_text::edit_finished);
}

void title_text::edit_finished()
{
    setFrame(false);
    setReadOnly(true);
    emit title_changed(text());
}

void title_text::mousePressEvent(QMouseEvent* event)
{
    QLineEdit::mousePressEvent(event);
    if (isReadOnly())
    {
        setReadOnly(false);
        setFrame(true);
    }
}

}
