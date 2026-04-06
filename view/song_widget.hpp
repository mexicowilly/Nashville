#pragma once

#include "../model/song.hpp"
#include "song_body_widget.hpp"
#include <QWidget>
#include <QScrollArea>
#include <QPrinter>

namespace nashville::view
{

// Displays the song name.
class SongHeaderWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SongHeaderWidget(const model::song& song, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const model::song& song_;
};

// Top-level widget: header + scrollable body.
class SongWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SongWidget(const model::song& song, QWidget* parent = nullptr);

    // Print the chart to printer.
    void print(QPrinter* printer);

    // Call when song data changes.
    void refresh();

private:
    const model::song& song_;
    SongHeaderWidget*  header_ = nullptr;
    SongBodyWidget*    body_   = nullptr;
    QScrollArea*       scroll_ = nullptr;
};

} // namespace nashville::view
