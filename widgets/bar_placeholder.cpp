#include "bar_placeholder.hpp"
#include <QGuiApplication>
#include <QScreen>

namespace nashville::widgets
{

bar_placeholder::bar_placeholder(QWidget* parent)
    : QFrame(parent)
{
    setFixedWidth(QGuiApplication::primaryScreen()->logicalDotsPerInchX() * bar::DEFAULT_MINIMUM_WIDTH);
    setMinimumHeight(QGuiApplication::primaryScreen()->logicalDotsPerInchY() * bar::DEFAULT_MINIMUM_HEIGHT);
    setFrameShape(QFrame::Box);
}

}
