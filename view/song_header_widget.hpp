#pragma once

#include "../model/song.hpp"
#include "song_body_widget.hpp"
#include <QWidget>
#include <QScrollArea>
#include <QPrinter>

namespace nashville::view
{

// Displays the song name.
class song_header_widget : public QWidget
{
    Q_OBJECT
public:
    explicit song_header_widget(const model::song& song, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const model::song& song_;
};

// Top-level widget: header + scrollable body.
class song_widget : public QWidget
{
    Q_OBJECT
public:
    explicit song_widget(const model::song& song, QWidget* parent = nullptr);

    // Print the chart to printer.
    void print(QPrinter* printer);

    // Call when song data changes.
    void refresh();

private:
    const model::song& song_;
    song_header_widget* header_ = nullptr;
    song_body_widget*   body_   = nullptr;
    QScrollArea*       scroll_ = nullptr;
};

} // namespace nashville::view
