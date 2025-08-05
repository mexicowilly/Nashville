#include "app.hpp"
#include <QCoreApplication>

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName("Will Mason");
    QCoreApplication::setApplicationName("Studio B");
    nashville::app app(argc, argv);
    return app.run();
}
