#include "bar.hpp"
#include "chord.hpp"
#include <QHBoxLayout>
#include <QMouseEvent>

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
    setLayout(new QHBoxLayout);
    setFocusPolicy(Qt::StrongFocus);
    setLineWidth(1);
}

void bar::focusInEvent(QFocusEvent* evt)
{
    QFrame::focusInEvent(evt);
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

void bar::mousePressEvent(QMouseEvent* evt)
{
    QFrame::mousePressEvent(evt);
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
