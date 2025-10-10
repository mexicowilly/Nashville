#include "key_column.hpp"
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QScreen>

namespace nashville::widgets
{

key_column::key_column()
{
    setLayout(new QVBoxLayout);
    // Make this always three quarters of an inch
    setFixedWidth(QGuiApplication::primaryScreen()->logicalDotsPerInchX() * .75);
}

}
