#pragma once

#include <QLabel>
#include "../model/chord.hpp"

namespace nashville::widgets
{

class chord : public QWidget
{
    Q_OBJECT

public:
    chord(QWidget* parent);
    chord(QWidget* parent, const model::chord& mdl);

    bool selected() const;
    chord& selected(bool state);
    chord& underline_thickness(unsigned th);
    chord& underlined(bool state);

private:
    void set_chord_label_text();

    model::chord model_;
    QLabel* chord_text_;
    QWidget* underbar_;
    bool selected_;
};

inline bool chord::selected() const
{
    return selected_;
}

}
