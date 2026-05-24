#include "annotations.hpp"
#include <algorithm>

namespace nashville::model
{

annotations::annotations()
    : loggable("annotations")
{
}

text_box& annotations::add_text_box(const QRectF& rect, const QString& text)
{
    text_box tb;
    tb.id   = next_id_++;
    tb.rect = rect;
    tb.text = text;
    text_boxes_.push_back(std::move(tb));
    return text_boxes_.back();
}

connector& annotations::add_connector(const QPointF& a, const QPointF& b)
{
    // Two-free-endpoint convenience overload.  Delegates to the general
    // form so we have a single id-assignment path; bonus: a future
    // change to the connector struct only has to be reflected in the
    // (endpoint, endpoint) overload.
    return add_connector(connector_endpoint::make_free(a),
                         connector_endpoint::make_free(b));
}

connector& annotations::add_connector(const connector_endpoint& a,
                                      const connector_endpoint& b)
{
    connector c;
    c.id    = next_id_++;
    c.start = a;
    c.end   = b;
    // arrow_at_end defaults to true (set in the struct definition) so
    // we don't override here.  Callers that need a line (no arrowhead)
    // toggle the flag on the returned reference — same idiom the
    // annotation_layer rubber-band-commit path uses.
    connectors_.push_back(c);
    return connectors_.back();
}

text_box* annotations::find_text_box(std::uint64_t id)
{
    auto it = std::find_if(text_boxes_.begin(), text_boxes_.end(),
                           [id](const text_box& tb) { return tb.id == id; });
    return it == text_boxes_.end() ? nullptr : &*it;
}

const text_box* annotations::find_text_box(std::uint64_t id) const
{
    auto it = std::find_if(text_boxes_.begin(), text_boxes_.end(),
                           [id](const text_box& tb) { return tb.id == id; });
    return it == text_boxes_.end() ? nullptr : &*it;
}

connector* annotations::find_connector(std::uint64_t id)
{
    auto it = std::find_if(connectors_.begin(), connectors_.end(),
                           [id](const connector& c) { return c.id == id; });
    return it == connectors_.end() ? nullptr : &*it;
}

const connector* annotations::find_connector(std::uint64_t id) const
{
    auto it = std::find_if(connectors_.begin(), connectors_.end(),
                           [id](const connector& c) { return c.id == id; });
    return it == connectors_.end() ? nullptr : &*it;
}

bool annotations::remove_text_box(std::uint64_t id)
{
    auto it = std::find_if(text_boxes_.begin(), text_boxes_.end(),
                           [id](const text_box& tb) { return tb.id == id; });
    if (it == text_boxes_.end())
        return false;

    // Convert any connector endpoints glued to this text box into
    // free endpoints at the anchor's last-resolved position, so the
    // connector survives the deletion visually (it just unsticks).
    // This matches "preserve user content" — silently removing
    // connectors when a referenced text box is deleted would be
    // surprising.  If no resolver was installed (test-only path), we
    // fall back to (0, 0); the view always installs one in practice.
    auto resolve_or_zero = [this](std::uint64_t tb_id, unsigned idx) -> QPointF {
        if (resolve_anchor_)
            return resolve_anchor_(tb_id, idx);
        return QPointF(0, 0);
    };
    for (auto& c : connectors_)
    {
        if (c.start.k == connector_endpoint::kind::text_box_anchor &&
            c.start.text_box_id == id)
        {
            const QPointF p = resolve_or_zero(c.start.text_box_id,
                                              c.start.anchor_index);
            c.start = connector_endpoint::make_free(p);
        }
        if (c.end.k == connector_endpoint::kind::text_box_anchor &&
            c.end.text_box_id == id)
        {
            const QPointF p = resolve_or_zero(c.end.text_box_id,
                                              c.end.anchor_index);
            c.end = connector_endpoint::make_free(p);
        }
    }

    text_boxes_.erase(it);
    return true;
}

bool annotations::remove_connector(std::uint64_t id)
{
    auto it = std::find_if(connectors_.begin(), connectors_.end(),
                           [id](const connector& c) { return c.id == id; });
    if (it == connectors_.end())
        return false;
    connectors_.erase(it);
    return true;
}

void annotations::load(std::vector<text_box> tbs, std::vector<connector> cs)
{
    text_boxes_ = std::move(tbs);
    connectors_ = std::move(cs);

    // Reseed next_id_ so subsequent add_*() doesn't collide with
    // loaded ids.  +1 because next_id_ is the *next* value to hand out.
    std::uint64_t max_id = 0;
    for (const auto& t : text_boxes_)
        max_id = std::max(max_id, t.id);
    for (const auto& c : connectors_)
        max_id = std::max(max_id, c.id);
    next_id_ = max_id + 1;
}

} // namespace nashville::model
