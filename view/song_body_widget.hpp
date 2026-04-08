#pragma once

#include "../model/song.hpp"
#include "chord_renderer.hpp"
#include "bar_renderer.hpp"
#include "layout_structs.hpp"
#include <QWidget>
#include <vector>

namespace nashville::view
{

class song_body_widget : public QWidget
{
    Q_OBJECT

public:
    explicit song_body_widget(const model::song& song, QWidget* parent = nullptr);

    // Call after font changes or song data changes.
    void rebuild();

    // For printing: same layout/paint logic targeting an arbitrary rect.
    void paint_to_rect(QPainter& painter, const QRectF& page_rect) const;

    int margin_width() const { return margin_width_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // --- Layout ---
    void compute_layout(const QRectF& content_rect);
    qreal plain_bar_height() const;
    qreal duration_bar_height() const;
    qreal section_label_height() const;

    // --- Painting ---
    void paint_margin(QPainter& painter, const QRectF& margin_rect) const;
    void paint_divider(QPainter& painter) const;
    void paint_line(QPainter& painter, const line_layout& line) const;
    void paint_section_label(QPainter& painter,
                           const QString& label,
                           const QRectF& line_rect) const;
    void paint_continuation_dot(QPainter& painter,
                               const QRectF& preceding_bar_rect) const;

    // --- Draggable divider ---
    bool near_divider(int x) const;
    bool dragging_divider_ = false;
    int  drag_start_x_      = 0;
    int  drag_start_margin_ = 0;

    // --- Constants ---
    static constexpr qreal k_line_spacing       = 16.0;
    static constexpr qreal k_inter_bar_spacing   = 6.0;
    static constexpr qreal k_content_padding    = 12.0;
    static constexpr int   k_divider_hit_width  = 5;
    static constexpr int   k_min_margin_width   = 60;
    static constexpr int   k_max_margin_width   = 200;
    static constexpr int   k_default_margin_width = 100;

    // --- Data ---
    const model::song&      song_;
    std::vector<line_layout> lines_;
    chord_renderer::Fonts    fonts_;
    int                     margin_width_ = k_default_margin_width;

    void init_fonts();
};

} // namespace nashville::view
