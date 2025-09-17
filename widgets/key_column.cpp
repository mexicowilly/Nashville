#include "key_column.hpp"
#include <QVBoxLayout>

namespace nashville::widgets
{

key_column::key_column()
{
    setLayout(new QVBoxLayout);
    setMaximumWidth(50);
}

}
