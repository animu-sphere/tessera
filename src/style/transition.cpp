#include <tessera/style/style_transitions.hpp>
#include <tessera/style/transition.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace tessera {
namespace {

// x(s) and y(s) of a cubic Bezier from (0, 0) to (1, 1) through (x1, y1) and (x2, y2).
double bezier(double s, double p1, double p2) {
    const double u = 1 - s;
    return 3 * u * u * s * p1 + 3 * u * s * s * p2 + s * s * s;
}
double cubic(double t, double x1, double y1, double x2, double y2) {
    // x(s) is monotonic for control x in [0, 1]; a fixed bisection keeps results reproducible.
    double low = 0, high = 1;
    for (int i = 0; i < 48; ++i) {
        const double middle = (low + high) / 2;
        (bezier(middle, x1, x2) < t ? low : high) = middle;
    }
    return bezier((low + high) / 2, y1, y2);
}

float mix(float a, float b, double t) { return static_cast<float>(a + (double(b) - a) * t); }
Point mix(Point a, Point b, double t) { return {mix(a.x, b.x, t), mix(a.y, b.y, t)}; }
float unit(double value) { return static_cast<float>(std::clamp(value, 0.0, 1.0)); }
// Premultiplied interpolation of sRGB-encoded components, so a fade from transparent keeps the
// target hue instead of passing through the transparent color's black.
Color mix(const Color& a, const Color& b, double t) {
    const double alpha = a.a + (double(b.a) - a.a) * t;
    if (alpha <= 0) return {0, 0, 0, 0};
    const auto channel = [&](float from, float to) {
        return unit((double(from) * a.a + (double(to) * b.a - double(from) * a.a) * t) / alpha);
    };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), unit(alpha)};
}
Color clear(Color c) { return {c.r, c.g, c.b, 0}; }

// An absent effect fades as the present one with a transparent color; shadows of different kinds
// cannot blend and switch to the target.
std::optional<BoxShadow> mix(const std::optional<BoxShadow>& a, const std::optional<BoxShadow>& b, double t) {
    if (!a && !b) return std::nullopt;
    auto from = a ? *a : BoxShadow{b->offset, b->softness, clear(b->color), b->inset};
    auto to = b ? *b : BoxShadow{a->offset, a->softness, clear(a->color), a->inset};
    if (from.inset != to.inset) return b;
    return BoxShadow{mix(from.offset, to.offset, t), std::max(0.f, mix(from.softness, to.softness, t)),
                     mix(from.color, to.color, t), to.inset};
}
std::optional<Outline> mix(const std::optional<Outline>& a, const std::optional<Outline>& b, double t) {
    if (!a && !b) return std::nullopt;
    auto from = a ? *a : Outline{b->width, b->offset, clear(b->color), b->dash_length, b->gap_length, b->dash_offset};
    auto to = b ? *b : Outline{a->width, a->offset, clear(a->color), a->dash_length, a->gap_length, a->dash_offset};
    const auto length = [&](float x, float y) { return std::max(0.f, mix(x, y, t)); };
    Outline result{length(from.width, to.width),           length(from.offset, to.offset),
                   mix(from.color, to.color, t),           length(from.dash_length, to.dash_length),
                   length(from.gap_length, to.gap_length), mix(from.dash_offset, to.dash_offset, t)};
    // Dash lengths interpolate; a gap without a dash would be invalid, so it collapses with the dash.
    if (result.dash_length <= 0) result.gap_length = 0;
    return result;
}

// Field accessors in TransitionProperties order.
template<class F> void each_property(const TransitionProperties& p, F&& f) {
    f(0, p.opacity, &ResolvedStyle::opacity);
    f(1, p.background, &ResolvedStyle::background);
    f(2, p.border_color, &ResolvedStyle::border_color);
    f(3, p.color, &ResolvedStyle::color);
    f(4, p.corner_radius, &ResolvedStyle::corner_radius);
    f(5, p.box_shadow, &ResolvedStyle::box_shadow);
    f(6, p.outline, &ResolvedStyle::outline);
}

} // namespace

double ease(Easing easing, double t) noexcept {
    if (!(t > 0)) return 0;
    if (t >= 1) return 1;
    switch (easing) {
        case Easing::linear:
            return t;
        case Easing::ease:
            return cubic(t, .25, .1, .25, 1);
        case Easing::ease_in:
            return cubic(t, .42, 0, 1, 1);
        case Easing::ease_out:
            return cubic(t, 0, 0, .58, 1);
        case Easing::ease_in_out:
            return cubic(t, .42, 0, .58, 1);
    }
    return t;
}

std::vector<Diagnostic> StyleTransitions::update(const UiTree& tree, std::span<const ResolvedStyle> targets,
                                                 double now) {
    std::vector<Diagnostic> errors;
    if (targets.size() != tree.size())
        errors.push_back({"transition_style_count",
                          Severity::error,
                          "/styles",
                          "Supply one resolved style per node of the accepted tree.",
                          {}});
    if (!std::isfinite(now) || (last_ && now < *last_))
        errors.push_back({"transition_time",
                          Severity::error,
                          "/now",
                          "Use a finite host time no earlier than the previous update.",
                          {}});
    for (std::size_t i = 0; i < targets.size(); ++i)
        for (auto& d : validate(targets[i], "/styles/" + std::to_string(i))) errors.push_back(std::move(d));
    if (!errors.empty()) return errors;

    const auto presented = present(now);
    std::vector<std::optional<Node>> nodes(targets.size());
    std::unordered_map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto* node = tree.get({tree.root().tree, static_cast<std::uint32_t>(i)});
        if (!node || !node->id) continue;
        ids.emplace(*node->id, i);
        const auto previous = ids_.find(*node->id);
        if (previous == ids_.end()) continue;
        const auto& old_target = targets_[previous->second];
        const auto& old_shown = presented[previous->second];
        const auto& old_node = nodes_[previous->second];
        const auto& target = targets[i];
        const auto* transition = target.transition ? &*target.transition : nullptr;
        Node next;
        next.from = target;
        bool tracked = false;
        each_property(transition ? transition->properties : TransitionProperties{},
                      [&](std::size_t p, bool named, auto field) {
                          if (!named) return;
                          if (!(old_target.*field == target.*field)) {
                              if (transition->duration <= 0 || old_shown.*field == target.*field) return;
                              next.from.*field = old_shown.*field;
                              next.tracks[p] = Track{now, transition->duration, transition->easing};
                              tracked = true;
                          } else if (old_node && old_node->tracks[p] &&
                                     now < old_node->tracks[p]->start + old_node->tracks[p]->duration) {
                              // Unchanged target: the running transition continues toward it.
                              next.from.*field = old_node->from.*field;
                              next.tracks[p] = old_node->tracks[p];
                              tracked = true;
                          }
                      });
        if (tracked) nodes[i] = std::move(next);
    }
    targets_.assign(targets.begin(), targets.end());
    nodes_ = std::move(nodes);
    ids_ = std::move(ids);
    last_ = now;
    return {};
}

std::vector<ResolvedStyle> StyleTransitions::present(double now) const {
    auto result = targets_;
    if (!std::isfinite(now)) return result;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (!nodes_[i]) continue;
        const auto& node = *nodes_[i];
        auto& shown = result[i];
        each_property(TransitionProperties::all(), [&](std::size_t p, bool, auto field) {
            const auto& track = node.tracks[p];
            if (!track) return;
            const double linear = (now - track->start) / track->duration;
            if (linear >= 1) return;
            shown.*field = mix(node.from.*field, targets_[i].*field, ease(track->easing, linear));
        });
    }
    return result;
}

std::optional<double> StyleTransitions::settles_at() const {
    std::optional<double> end;
    for (const auto& node : nodes_)
        if (node)
            for (const auto& track : node->tracks)
                if (track) end = std::max(end.value_or(track->start + track->duration), track->start + track->duration);
    return end;
}

bool StyleTransitions::running(double now) const {
    const auto end = settles_at();
    return end && std::isfinite(now) && now < *end;
}

void StyleTransitions::settle() {
    for (auto& node : nodes_) node.reset();
}

void StyleTransitions::clear() {
    targets_.clear();
    nodes_.clear();
    ids_.clear();
    last_.reset();
}

} // namespace tessera
