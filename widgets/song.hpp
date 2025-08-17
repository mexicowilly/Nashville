#pragma once

#include "../model/song.hpp"
#include <QWidget>

namespace nashville::widgets
{

class song : public QWidget
{
    Q_OBJECT

public:
    song(QWidget* parent);
    song(QWidget* parent, const model::song& mdl);

private slots:
    void key_changed(const QString& key);
    void tempo_changed(const QString& temp);
    void time_signature_changed(const QString& ts);
    void title_changed(const QString& t);

private:
    model::song model_;
};

}
