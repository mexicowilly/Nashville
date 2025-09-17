#include "bar.hpp"
#include "chord.hpp"
#include <QHBoxLayout>
#include <QMouseEvent>

namespace
{

const QString CARRIAGE_RETURN_SYMBOL(u'\u21b5');
const QString BULLET_SYMBOL(u'\u2022');

}

namespace nashville::widgets
{

bar::bar(QWidget* parent)
    : bar(parent, model::bar())
{
}

bar::bar(QWidget* parent, const model::bar& mdl)
    : QFrame(parent),
      model_(mdl)
{
    setMinimumWidth(DEFAULT_MINIMUM_WIDTH);
    setMinimumHeight(DEFAULT_MINIMUM_HEIGHT);
    setLayout(new QHBoxLayout);
    setFocusPolicy(Qt::StrongFocus);
    setLineWidth(1);
    setAutoFillBackground(true);
    if (model_.empty())
    {
        auto pal = palette();
        pal.setColor(QPalette::Window, Qt::lightGray);
        setPalette(pal);
    }
}

void bar::focusInEvent(QFocusEvent* evt)
{
    QFrame::focusInEvent(evt);
    evt->setAccepted(true);
    setFrameShape(QFrame::Box);
    if (model_.empty())
    {
        // display a line editor for bar input
    }
}

void bar::focusOutEvent(QFocusEvent* evt)
{
    QFrame::focusOutEvent(evt);
    setFrameShape(QFrame::NoFrame);
}

void bar::mouseReleaseEvent(QMouseEvent* evt)
{
    QFrame::mouseReleaseEvent(evt);
    evt->setAccepted(true);
    if (!children().empty())
    {
        auto chord_clicked = childAt(evt->position().toPoint());
        for (auto child : children())
        {
            auto c = dynamic_cast<chord*>(child);
            if (c != nullptr)
            {
                if (c->selected())
                    c->selected(false);
                else if (c == chord_clicked)
                    c->selected(true);
            }
        }
    }
}

}
