#include <tessera/input/drag_drop.hpp>
#include "../detail/drag_input.hpp"
#include "../detail/targeting.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace tessera {
namespace {

bool finite(Point point) { return std::isfinite(point.x) && std::isfinite(point.y); }

// RFC 6838 restricted names, lowercase so type IDs compare exactly.
bool type_token(std::string_view token) {
    if (token.empty() || token.size() > 127) return false;
    const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!alnum(token.front())) return false;
    return std::all_of(token.begin(), token.end(), [&](char c) {
        return alnum(c) || c == '!' || c == '#' || c == '$' || c == '&' || c == '-' || c == '^' || c == '_' ||
               c == '.' || c == '+';
    });
}
bool type_id(std::string_view type) {
    const auto slash = type.find('/');
    return slash != std::string_view::npos && type_token(type.substr(0, slash)) && type_token(type.substr(slash + 1));
}

void check_type(std::string_view type, const std::string& path, std::vector<Diagnostic>& errors) {
    if (!type_id(type))
        errors.push_back({"invalid_drag_type", Severity::error, path,
                          "Use a lowercase MIME-like \"type/subtype\" payload type ID."});
}
void check_operations(const DragOperations& operations, const std::string& path, std::vector<Diagnostic>& errors) {
    if (operations.empty())
        errors.push_back({"drag_operations", Severity::error, path, "Allow at least one of move, copy and link."});
}
void check_id(const std::string& id, const std::string& path, std::set<std::string_view>& seen,
              std::vector<Diagnostic>& errors) {
    if (id.empty())
        errors.push_back({"invalid_identifier", Severity::error, path, "Supply a nonempty stable author ID."});
    else if (!seen.insert(id).second)
        errors.push_back({"duplicate_identifier", Severity::error, path, "List each stable author ID once."});
}

std::vector<Diagnostic> source_errors(const DragSource& source, const std::string& path) {
    std::vector<Diagnostic> errors;
    check_type(source.type, path + "/type", errors);
    check_operations(source.operations, path + "/operations", errors);
    return errors;
}

std::vector<Diagnostic> option_errors(const DragDropOptions& options) {
    if (std::isfinite(options.threshold) && options.threshold >= 0) return {};
    return {{"drag_drop_options", Severity::error, "/options", "Supply a finite nonnegative drag threshold."}};
}

DragOperations intersect(const DragOperations& a, const DragOperations& b) {
    return {a.move && b.move, a.copy && b.copy, a.link && b.link};
}

// The innermost eligible source on the ancestry of the pressed hit.
const DragSource* pressed_source(const HitTestInput& input, const detail::Targeting& targeting,
                                 const DragDropScope& scope, Point position) {
    const auto hit = targeting.hit(position);
    if (!hit) return nullptr;
    const auto& boxes = input.layout->boxes;
    for (auto at = targeting.box(*hit); at != no_layout_parent; at = boxes[at].parent) {
        const auto& id = input.tree->get(boxes[at].node)->id;
        if (!id || !targeting.eligible(static_cast<std::size_t>(at))) continue;
        const auto found = std::find_if(scope.sources.begin(), scope.sources.end(),
                                        [&](const DragSource& source) { return source.id == *id; });
        if (found != scope.sources.end()) return &*found;
    }
    return nullptr;
}

std::optional<DragOver> target_at(const HitTestInput& input, const detail::Targeting& targeting,
                                  const DragDropScope& scope, const DragSource& source, Point position,
                                  Modifiers modifiers) {
    const auto hit = targeting.hit(position);
    if (!hit) return {};
    const auto& boxes = input.layout->boxes;
    for (auto at = targeting.box(*hit); at != no_layout_parent; at = boxes[at].parent) {
        const auto& id = input.tree->get(boxes[at].node)->id;
        if (!id || !targeting.eligible(static_cast<std::size_t>(at))) continue;
        const auto found = std::find_if(scope.targets.begin(), scope.targets.end(),
                                        [&](const DropTarget& target) { return target.id == *id; });
        if (found == scope.targets.end() ||
            std::find(found->types.begin(), found->types.end(), source.type) == found->types.end())
            continue;
        if (const auto operation = drag_operation(intersect(source.operations, found->operations), modifiers))
            return DragOver{*id, *operation};
    }
    return {};
}

Modifiers modifiers_of(const InputEvent& event) {
    if (const auto* move = std::get_if<PointerMove>(&event.data)) return move->modifiers;
    if (const auto* up = std::get_if<PointerUp>(&event.data)) return up->modifiers;
    return {};
}

} // namespace

bool DragOperations::contains(DragOperation operation) const noexcept {
    switch (operation) {
        case DragOperation::move:
            return move;
        case DragOperation::copy:
            return copy;
        case DragOperation::link:
            return link;
    }
    return false;
}

std::optional<DragOperation> drag_operation(DragOperations allowed, Modifiers modifiers) noexcept {
    std::optional<DragOperation> requested;
    if (modifiers.control && modifiers.shift)
        requested = DragOperation::link;
    else if (modifiers.control)
        requested = DragOperation::copy;
    else if (modifiers.shift)
        requested = DragOperation::move;
    if (requested) return allowed.contains(*requested) ? requested : std::nullopt;
    for (const auto operation : {DragOperation::move, DragOperation::copy, DragOperation::link})
        if (allowed.contains(operation)) return operation;
    return {};
}

std::vector<Diagnostic> validate(const DragDropScope& scope) {
    std::vector<Diagnostic> errors;
    std::set<std::string_view> sources, targets;
    for (std::size_t i = 0; i < scope.sources.size(); ++i) {
        const auto path = "/sources/" + std::to_string(i);
        check_id(scope.sources[i].id, path + "/id", sources, errors);
        auto source = source_errors(scope.sources[i], path);
        errors.insert(errors.end(), source.begin(), source.end());
    }
    for (std::size_t i = 0; i < scope.targets.size(); ++i) {
        const auto& target = scope.targets[i];
        const auto path = "/targets/" + std::to_string(i);
        check_id(target.id, path + "/id", targets, errors);
        if (target.types.empty())
            errors.push_back({"invalid_drag_type", Severity::error, path + "/types", "Accept at least one type."});
        std::set<std::string_view> types;
        for (std::size_t t = 0; t < target.types.size(); ++t) {
            const auto at = path + "/types/" + std::to_string(t);
            check_type(target.types[t], at, errors);
            if (!types.insert(target.types[t]).second)
                errors.push_back({"duplicate_drag_type", Severity::error, at, "List each accepted type once."});
        }
        check_operations(target.operations, path + "/operations", errors);
    }
    return errors;
}

Result<std::optional<DragOver>> drop_target_at(const HitTestInput& input, const DragDropScope& scope,
                                               const DragSource& source, Point position, Modifiers modifiers) {
    auto errors = validate(input);
    auto scoped = validate(scope);
    auto dragged = source_errors(source, "/source");
    errors.insert(errors.end(), scoped.begin(), scoped.end());
    errors.insert(errors.end(), dragged.begin(), dragged.end());
    if (!finite(position))
        errors.push_back({"invalid_position", Severity::error, "/position", "Supply a finite root-logical position."});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const detail::Targeting targeting(input);
    return {target_at(input, targeting, scope, source, position, modifiers), {}};
}

Result<DragDropDispatchResult> DragDropDispatcher::refresh(const HitTestInput& input, const DragDropScope& scope) {
    auto errors = validate(input);
    auto scoped = validate(scope);
    auto options = option_errors(options_);
    errors.insert(errors.end(), scoped.begin(), scoped.end());
    errors.insert(errors.end(), options.begin(), options.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    DragDropDispatchResult result;
    if (!drag_) return {result, {}};
    if (drag_->dragging) result.source = drag_->source;
    const detail::Targeting targeting(input);
    const auto node = input.tree->find(drag_->source.id);
    const bool listed = std::find(scope.sources.begin(), scope.sources.end(), drag_->source) != scope.sources.end();
    if (!listed || !node || !targeting.eligible(*node)) {
        result.ended = result.canceled = drag_->dragging;
        reset();
        return {result, {}};
    }
    if (drag_->dragging) {
        result.position = drag_->position;
        result.target = target_at(input, targeting, scope, drag_->source, drag_->position, drag_->modifiers);
    }
    return {result, {}};
}

Result<DragDropDispatchResult> DragDropDispatcher::dispatch(const HitTestInput& input, const DragDropScope& scope,
                                                            const InputEvent& event) {
    auto errors = validate(event);
    auto checked = validate(input);
    auto scoped = validate(scope);
    auto options = option_errors(options_);
    errors.insert(errors.begin(), checked.begin(), checked.end());
    errors.insert(errors.end(), scoped.begin(), scoped.end());
    errors.insert(errors.end(), options.begin(), options.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    // A replacement may already have removed the source.
    auto refreshed = refresh(input, scope);
    if (!refreshed) return refreshed;
    DragDropDispatchResult result;
    result.ended = refreshed.value->ended;
    result.canceled = refreshed.value->canceled;
    if (result.ended) result.source = refreshed.value->source;
    const detail::Targeting targeting(input);
    const auto dragging_result = [&](Point position, Modifiers modifiers) {
        drag_->position = position;
        drag_->modifiers = modifiers;
        result.consumed = true;
        result.source = drag_->source;
        result.position = position;
        result.target = target_at(input, targeting, scope, drag_->source, position, modifiers);
    };
    if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        if (drag_ || press->button != PointerButton::primary) return {result, {}};
        if (const auto* source = pressed_source(input, targeting, scope, press->position))
            drag_ = Drag{press->pointer, *source, press->position, press->position, press->modifiers};
        return {result, {}};
    }
    if (!drag_) return {result, {}};
    if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        if (move->pointer != drag_->pointer) return {result, {}};
        if (!drag_->dragging) {
            const double dx = static_cast<double>(move->position.x) - drag_->origin.x;
            const double dy = static_cast<double>(move->position.y) - drag_->origin.y;
            if (std::hypot(dx, dy) < options_.threshold) {
                drag_->position = move->position;
                drag_->modifiers = move->modifiers;
                return {result, {}};
            }
            drag_->dragging = result.started = true;
        }
        dragging_result(move->position, modifiers_of(event));
        return {result, {}};
    }
    if (const auto* release = std::get_if<PointerUp>(&event.data)) {
        if (release->pointer != drag_->pointer || release->button != PointerButton::primary) return {result, {}};
        if (drag_->dragging) {
            dragging_result(release->position, modifiers_of(event));
            result.position.reset();
            result.drop = std::move(result.target);
            result.target.reset();
            result.ended = true;
            result.canceled = !result.drop;
        }
        reset();
        return {result, {}};
    }
    if (const auto* lost = std::get_if<PointerCancel>(&event.data)) {
        if (lost->pointer != drag_->pointer) return {result, {}};
        if (drag_->dragging) result.source = drag_->source;
        result.consumed = result.ended = result.canceled = drag_->dragging;
        reset();
        return {result, {}};
    }
    if (detail::drag_escape(event) || detail::drag_interrupt(event)) {
        // Escape and Cancel belong to a drag; other input continues to ordinary routing.
        if (drag_->dragging) result.source = drag_->source;
        result.consumed = drag_->dragging && detail::drag_escape(event);
        result.ended = result.canceled = drag_->dragging;
        reset();
    }
    return {result, {}};
}

} // namespace tessera
