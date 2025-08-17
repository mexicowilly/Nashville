#pragma once

#include <QWidget>

namespace nashville::widgets
{

// This will have a column consisting of the key with a circle
// around it, a time signature, and the tempo.
//
// It will use signals to communicate changes to the song.
class key_column : public QWidget
{
    Q_OBJECT

public:
    key_column();

signals:
    void key_changed(const QString& key) const;
    void tempo_changed(const QString& temp) const;
    void time_signature_changed(const QString& ts) const;
};

}
