#pragma once

#include "../model/song.hpp"
#include "chord_renderer.hpp"
#include "bar_renderer.hpp"
#include "layout_structs.hpp"
#include <QWidget>
#include <vector>

namespace nashville::view
{

class SongBodyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SongBodyWidget(const model::song& song, QWidget* parent = nullptr);

    // Call after font changes or song data changes.
    void rebuild();

    // For printing: same layout/paint logic targeting an arbitrary rect.
    void paintToRect(QPainter& painter, const QRectF& pageRect) const;

    int marginWidth() const { return marginWidth_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // --- Layout ---
    void computeLayout(const QRectF& contentRect);
    qreal plainBarHeight() const;
    qreal durationBarHeight() const;
    qreal sectionLabelHeight() const;

    // --- Painting ---
    void paintMargin(QPainter& painter, const QRectF& marginRect) const;
    void paintDivider(QPainter& painter) const;
    void paintLine(QPainter& painter, const LineLayout& line) const;
    void paintSectionLabel(QPainter& painter,
                           const QString& label,
                           const QRectF& lineRect) const;
    void paintContinuationDot(QPainter& painter,
                               const QRectF& precedingBarRect) const;

    // --- Draggable divider ---
    bool nearDivider(int x) const;
    bool draggingDivider_ = false;
    int  dragStartX_      = 0;
    int  dragStartMargin_ = 0;

    // --- Constants ---
    static constexpr qreal kLineSpacing      = 16.0;
    static constexpr qreal kInterBarSpacing  = 6.0;
    static constexpr qreal kContentPadding   = 12.0;
    static constexpr int   kDividerHitWidth  = 5;
    static constexpr int   kMinMarginWidth   = 60;
    static constexpr int   kMaxMarginWidth   = 200;
    static constexpr int   kDefaultMarginWidth = 100;

    // --- Data ---
    const model::song&      song_;
    std::vector<LineLayout> lines_;
    ChordRenderer::Fonts    fonts_;
    int                     marginWidth_ = kDefaultMarginWidth;

    void initFonts();
};

} // namespace nashville::view
