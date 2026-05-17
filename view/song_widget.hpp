#pragma once

#include "../model/song.hpp"
#include "song_body_widget.hpp"
#include <QWidget>
#include <QScrollArea>
#include <QPrinter>

namespace nashville::view
{

// Top-level widget: scrollable page with title + body.
// The chart is always editable — clicking the title or any of the three
// margin elements (key, time signature, tempo) opens an editor dialog.
class song_widget : public QWidget
{
    Q_OBJECT
public:
    explicit song_widget(model::song& song, QWidget* parent = nullptr);

    // Print the chart to printer.
    void print(QPrinter* printer);

    // Call when song data changes from outside the widget.
    void refresh();

    // The body widget renders and owns the bar selection; expose it so
    // the main-window menus (defined in app.cpp) can drive bar-attribute
    // edits against whichever bars the user has selected in the chart.
    // Stable for the lifetime of this song_widget.
    song_body_widget* body() const { return body_; }

private:
    model::song&       song_;
    song_body_widget*  body_   = nullptr;
    QScrollArea*       scroll_ = nullptr;
};

} // namespace nashville::view
