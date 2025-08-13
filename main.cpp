#include "app.hpp"
#include <QCoreApplication>

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName("Will Mason");
    QCoreApplication::setApplicationName("Nashville");
    nashville::app app(argc, argv);
    return app.run();
}
