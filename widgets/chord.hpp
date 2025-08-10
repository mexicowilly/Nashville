#pragma once

#include <QLabel>
#include <QVBoxLayout>
#include "../model/chord.hpp"

namespace nashville::widgets
{

class chord : public QWidget
{
    Q_OBJECT

public:
    chord(QWidget* parent, model::chord& mdl);

private slots:
    void model_changed();

private:
    void set_chord_label_text();

    model::chord& model_;
    QVBoxLayout* layout_;
    QLabel* chord_text_;
};

}
