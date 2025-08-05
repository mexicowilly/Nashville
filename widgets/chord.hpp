#pragma once

#include <QLabel>
#include <QGridLayout>

namespace nashville::widgets
{

class chord : public QWidget
{
    Q_OBJECT

public:
    chord(QWidget* parent = nullptr);

private:
    QGridLayout* layout_;
    QLabel* chord_text_;
};

}
