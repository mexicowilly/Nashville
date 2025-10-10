#include "title_text.hpp"
#include <QFontDatabase>
#include <QMouseEvent>

namespace nashville::widgets
{

title_text::title_text()
{
    setFrame(false);
    setPlaceholderText("Title");
    setAlignment(Qt::AlignHCenter);
    setReadOnly(true);
    setFont(QFontDatabase::systemFont(QFontDatabase::TitleFont));
    connect(this, &title_text::editingFinished,
            this, &title_text::edit_finished);
}

void title_text::edit_finished()
{
    setFrame(false);
    setReadOnly(true);
    QFontMetrics fm(font());
    setFixedWidth(fm.boundingRect(text()).width() + 20);
    emit title_changed(text());
}

void title_text::mouseReleaseEvent(QMouseEvent* event)
{
    QLineEdit::mouseReleaseEvent(event);
    event->setAccepted(true);
    if (isReadOnly())
    {
        setReadOnly(false);
        setFrame(true);
    }
}

}
