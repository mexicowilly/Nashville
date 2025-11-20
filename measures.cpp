#include "measures.hpp"
#include <QGuiApplication>
#include <QScreen>

namespace nashville::measures
{

double inches_wide(double fraction)
{
    return QGuiApplication::primaryScreen()->logicalDotsPerInchX() * fraction;
}

double inches_tall(double fraction)
{
    return QGuiApplication::primaryScreen()->logicalDotsPerInchY() * fraction;
}

}

