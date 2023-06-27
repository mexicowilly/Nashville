#include "title_text.hpp"

namespace nashville::ui
{

title_text::title_text()
{
    setFrame(false);
}

void title_text::mousePressEvent(QMouseEvent* event)
{
    QLineEdit::mousePressEvent(event);
    if (isReadOnly())
        setReadOnly(false);
}

}