#include "song_header_widget.hpp"
#include <QVBoxLayout>
#include <QPainter>
#include <QFontMetricsF>
#include <QPrintDialog>
#include <cmath>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// song_header_widget
// ---------------------------------------------------------------------------
song_header_widget::song_header_widget(const model::song& song, QWidget* parent)
    : QWidget(parent), song_(song)
{
    QFont f("Georgia", 22, QFont::Bold);
    QFontMetricsF fm(f);
    setFixedHeight(static_cast<int>(fm.height() + 24.0));
}

void song_header_widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont titleFont("Georgia", 22, QFont::Bold);
    painter.setFont(titleFont);
    QFontMetricsF fm(titleFont);

    QString title = QString::fromStdString(song_.name());
    qreal x = (width()  - fm.horizontalAdvance(title)) / 2.0;
    qreal y = (height() + fm.ascent() - fm.descent())  / 2.0;
    painter.drawText(QPointF(x, y), title);

    // Subtle bottom rule
    painter.setPen(QPen(QColor(200, 200, 200), 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

// ---------------------------------------------------------------------------
// song_widget
// ---------------------------------------------------------------------------
song_widget::song_widget(const model::song& song, QWidget* parent)
    : QWidget(parent), song_(song)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = new song_header_widget(song_, this);
    layout->addWidget(header_);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    body_ = new song_body_widget(song_, scroll_);
    scroll_->setWidget(body_);

    layout->addWidget(scroll_, 1);
    setLayout(layout);
}

void song_widget::refresh()
{
    header_->update();
    body_->rebuild();
}

void song_widget::print(QPrinter* printer)
{
    QPrintDialog dialog(printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPainter painter(printer);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF pageRect = printer->pageRect(QPrinter::DevicePixel);

    // Header
    QFont titleFont("Georgia", 22, QFont::Bold);
    painter.setFont(titleFont);
    QFontMetricsF fm(titleFont);
    QString title  = QString::fromStdString(song_.name());
    qreal headerH  = fm.height() + 24.0;
    qreal titleX   = (pageRect.width() - fm.horizontalAdvance(title)) / 2.0;
    painter.drawText(QPointF(pageRect.left() + titleX,
                             pageRect.top() + fm.ascent() + 8.0), title);

    // Body
    QRectF bodyRect = pageRect.adjusted(0, headerH, 0, 0);
    body_->paint_to_rect(painter, bodyRect);
}

} // namespace nashville::view
