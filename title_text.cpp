#include "title_text.hpp"

namespace nashville::ui
{

title_text::title_text(QWidget& parent)
    : QLineEdit(&parent)
{
    setFrame(false);
    setPlaceholderText("Title");
    setAlignment(Qt::AlignHCenter);
    setReadOnly(true);
    connect(this, SIGNAL(editingFinished()), this, SLOT(edit_finished()));
}

void title_text::edit_finished()
{
    setFrame(false);
    setReadOnly(true);
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