#include <tessera/inspection/invariants.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <set>

namespace tessera {
namespace {
using Errors = std::vector<Diagnostic>;

void text(detail::Checker& check, std::string_view value, const std::string& path,
          std::size_t maximum, bool identifier = false) {
    if (value.size() > maximum)
        check.error("invariant_limit", path, "Keep this text within " + std::to_string(maximum) + " UTF-8 bytes.");
    else if (!detail::valid_utf8(value))
        check.error("invalid_utf8", path, "Supply valid UTF-8 text.");
    if (identifier && (value.empty() || std::any_of(value.begin(), value.end(),
        [](unsigned char c) { return c < 0x20 || c == 0x7f; })))
        check.error("invariant_name", path, "Supply a nonempty name without control characters.");
}
void target_schema(detail::Checker& check, const InspectionTarget& target, const std::string& path) {
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, AuthorIdTarget>) text(check, value.id, path + "/id", 256, true);
        else if constexpr (std::is_same_v<T, SemanticTarget>) {
            check.enumeration(value.role, SemanticRole::text, path + "/role");
            text(check, value.name, path + "/name", max_invariant_text_bytes);
        } else if constexpr (std::is_same_v<T, PathTarget>) {
            if (value.children.size() >= max_document_depth)
                check.error("invariant_limit", path + "/children", "Keep the target path within the document depth bound.");
        } else check.point(value.position, path + "/position");
    }, target);
}

// Guard the owned public snapshot before calling the ordinary resolver, which
// expects capture output. Traverse only canonical preorder edges, with bounded depth.
bool hierarchy(detail::Checker& check, const InspectionSnapshot& snapshot, std::uint32_t index,
               std::uint32_t parent, std::size_t depth, std::size_t& next) {
    const auto path = "/snapshot/elements/" + std::to_string(index);
    if (index != next || index >= snapshot.elements.size() || depth > max_document_depth) {
        check.error("invariant_snapshot", path, "Supply a complete captured tree in bounded preorder.");
        return false;
    }
    const auto& element = snapshot.elements[index];
    if (element.parent != parent || element.children.size() > max_document_nodes) {
        check.error("invariant_snapshot", path, "Preserve the captured parent and child indices.");
        return false;
    }
    ++next;
    for (const auto child : element.children)
        if (!hierarchy(check, snapshot, child, index, depth + 1, next)) return false;
    return true;
}
Errors snapshot_schema(const InspectionSnapshot& snapshot) {
    Errors errors;
    detail::Checker check(errors);
    if (!snapshot.generation || snapshot.elements.empty() || snapshot.elements.size() > max_document_nodes ||
        snapshot.semantics.nodes.size() > snapshot.elements.size()) {
        check.error("invariant_snapshot", "/snapshot", "Supply a nonempty captured generation within the document bounds.");
        return errors;
    }
    std::size_t next = 0;
    if (!hierarchy(check, snapshot, 0, no_inspection_parent, 1, next)) return errors;
    if (next != snapshot.elements.size()) {
        check.error("invariant_snapshot", "/snapshot/elements", "Every element must belong to the captured root.");
        return errors;
    }
    for (std::size_t i = 0; i < snapshot.elements.size(); ++i) {
        const auto& element = snapshot.elements[i];
        const auto path = "/snapshot/elements/" + std::to_string(i);
        if (element.geometry) {
            check.rect(element.geometry->border_box, path + "/geometry/border_box");
            if (element.geometry->clip) check.rect(*element.geometry->clip, path + "/geometry/clip");
        }
        if (element.semantic && (*element.semantic >= snapshot.semantics.nodes.size() ||
            snapshot.semantics.nodes[*element.semantic].node != NodeHandle{snapshot.generation, static_cast<std::uint32_t>(i)}))
            check.error("invariant_snapshot", path + "/semantic", "Preserve the captured semantic link and generation.");
    }
    for (std::size_t i = 0; i < snapshot.semantics.nodes.size(); ++i) {
        const auto& entry = snapshot.semantics.nodes[i];
        if (entry.node.tree != snapshot.generation || entry.node.index >= snapshot.elements.size() ||
            snapshot.elements[entry.node.index].semantic != i ||
            (entry.labelled_by && entry.labelled_by->tree != snapshot.generation))
            check.error("invariant_snapshot", "/snapshot/semantics/nodes/" + std::to_string(i),
                        "Preserve the captured semantic handles and element links.");
    }
    // Reuse the semantic schema's bounds, identities, and state/action validation.
    if (errors.empty()) {
        auto semantic_errors = validate(SemanticSnapshot{semantic_version, snapshot.generation,
            static_cast<std::uint32_t>(snapshot.elements.size()), own_semantic_records(snapshot.semantics)});
        for (auto& diagnostic : semantic_errors) {
            diagnostic.path = "/snapshot/semantics" + diagnostic.path;
            errors.push_back(std::move(diagnostic));
        }
    }
    return errors;
}
double right(const Rect& r) { return static_cast<double>(r.origin.x) + r.size.width; }
double bottom(const Rect& r) { return static_cast<double>(r.origin.y) + r.size.height; }
bool visible(const std::optional<GeometryObservation>& geometry) {
    if (!geometry || !geometry->visible || geometry->border_box.size.width <= 0 || geometry->border_box.size.height <= 0)
        return false;
    if (!geometry->clip) return true;
    const auto& box = geometry->border_box;
    const auto& clip = *geometry->clip;
    return std::min(right(box), right(clip)) > std::max(box.origin.x, clip.origin.x) &&
        std::min(bottom(box), bottom(clip)) > std::max(box.origin.y, clip.origin.y);
}
void diagnostic(InvariantResult& result, std::string code, const std::string& path, std::string message,
                std::size_t observation = 0) {
    std::optional<std::size_t> offset;
    if (observation < result.observations.size() && result.observations[observation].source &&
        result.observations[observation].source->mapped)
        offset = result.observations[observation].source->span.begin;
    result.diagnostics.push_back({std::move(code), Severity::error, path, std::move(message), offset});
}
std::optional<std::uint32_t> resolve(InvariantResult& result, const InspectionSnapshot& snapshot,
                                   const InspectionTarget& target, const std::string& base,
                                   std::string_view field) {
    auto resolved = resolve_target(snapshot, target, result.declaration.scope);
    if (!resolved) {
        for (auto& cause : resolved.diagnostics) {
            if (cause.path.starts_with("/target")) cause.path = base + "/" + std::string(field) + cause.path.substr(7);
            else cause.path = base + cause.path;
            result.diagnostics.push_back(std::move(cause));
        }
        return {};
    }
    const auto& element = snapshot.elements[resolved.value->index];
    InvariantObservation observation;
    observation.target = *resolved.value;
    observation.source = element.source;
    if (element.geometry) {
        observation.bounds = element.geometry->border_box;
        observation.clip = element.geometry->clip;
    }
    result.observations.push_back(std::move(observation));
    return resolved.value->index;
}
} // namespace

bool InvariantReport::passed() const noexcept {
    return std::all_of(results.begin(), results.end(), [](const auto& result) { return result.status == InvariantStatus::passed; });
}
Result<InvariantReport> evaluate_invariants(const InvariantInput& input) {
    Errors errors;
    detail::Checker check(errors);
    if (!input.snapshot) check.error("missing_input", "/snapshot", "Supply one captured inspection generation.");
    if (input.invariants.size() > max_ui_invariants)
        check.error("invariant_limit", "/invariants", "Supply at most 256 invariants per report.");
    if (!errors.empty()) return {{}, std::move(errors)};
    std::set<std::string, std::less<>> names;
    bool needs_viewport = false;
    for (std::size_t i = 0; i < input.invariants.size(); ++i) {
        const auto& declaration = input.invariants[i];
        const auto path = "/invariants/" + std::to_string(i);
        text(check, declaration.name, path + "/name", 256, true);
        if (!names.insert(declaration.name).second)
            check.error("invariant_duplicate_name", path + "/name", "Give every invariant a unique report name.");
        std::visit([&](const auto& predicate) {
            using T = std::decay_t<decltype(predicate)>;
            target_schema(check, predicate.target, path + "/target");
            if constexpr (std::is_same_v<T, SemanticNameInvariant>) text(check, predicate.expected, path + "/expected", max_invariant_text_bytes);
            if constexpr (std::is_same_v<T, InsideViewportInvariant> || std::is_same_v<T, NoOverlapInvariant>)
                check.non_negative(predicate.tolerance, path + "/tolerance");
            if constexpr (std::is_same_v<T, InsideViewportInvariant>) needs_viewport = true;
            if constexpr (std::is_same_v<T, NoOverlapInvariant>) target_schema(check, predicate.other, path + "/other");
        }, declaration.predicate);
    }
    if (input.viewport) {
        check.positive(input.viewport->width, "/viewport/width");
        check.positive(input.viewport->height, "/viewport/height");
    } else if (needs_viewport) check.error("missing_input", "/viewport", "Declare the logical viewport of this captured generation.");
    if (!errors.empty()) return {{}, std::move(errors)};
    errors = snapshot_schema(*input.snapshot);
    if (!errors.empty()) return {{}, std::move(errors)};

    const auto& snapshot = *input.snapshot;
    InvariantReport report{snapshot.generation, input.viewport, snapshot.source_file, {}};
    for (std::size_t i = 0; i < input.invariants.size(); ++i) {
        InvariantResult result{input.invariants[i]};
        const auto path = "/invariants/" + std::to_string(i);
        std::visit([&](const auto& predicate) {
            using T = std::decay_t<decltype(predicate)>;
            const auto target = resolve(result, snapshot, predicate.target, path, "target");
            if (!target) return;
            const auto& element = snapshot.elements[*target];
            bool passed = false;
            if constexpr (std::is_same_v<T, VisibleInvariant>) {
                result.observations[0].state = visible(element.geometry);
                passed = *result.observations[0].state == predicate.expected;
            } else if constexpr (std::is_same_v<T, EnabledInvariant> || std::is_same_v<T, FocusableInvariant> ||
                                 std::is_same_v<T, SemanticNameInvariant>) {
                if (!element.semantic) {
                    diagnostic(result, "invariant_unavailable", path + "/target", "This target has no exposed semantic entry; choose a semantic element.");
                    return;
                }
                const auto& entry = snapshot.semantics.nodes[*element.semantic];
                if constexpr (std::is_same_v<T, SemanticNameInvariant>) {
                    result.observations[0].semantic_name = entry.name;
                    passed = entry.name == predicate.expected;
                } else {
                    if constexpr (std::is_same_v<T, EnabledInvariant>) result.observations[0].state = entry.enabled;
                    else result.observations[0].state = entry.focusable;
                    passed = *result.observations[0].state == predicate.expected;
                }
            } else if constexpr (std::is_same_v<T, InsideViewportInvariant>) {
                if (!element.geometry) {
                    diagnostic(result, "invariant_unavailable", path + "/target", "This target has no displayed geometry.");
                    return;
                }
                const auto& bounds = element.geometry->border_box;
                const double tolerance = predicate.tolerance;
                passed = bounds.origin.x >= -tolerance && bounds.origin.y >= -tolerance &&
                    right(bounds) <= static_cast<double>(input.viewport->width) + tolerance &&
                    bottom(bounds) <= static_cast<double>(input.viewport->height) + tolerance;
            } else {
                const auto other = resolve(result, snapshot, predicate.other, path, "other");
                if (!other) return;
                if (*other == *target) {
                    diagnostic(result, "invariant_same_target", path + "/other", "Compare two distinct elements.", 1);
                    return;
                }
                if (!element.geometry || !snapshot.elements[*other].geometry) {
                    const auto missing = element.geometry ? 1u : 0u;
                    diagnostic(result, "invariant_unavailable", path + (missing ? "/other" : "/target"), "This target has no displayed geometry.", missing);
                    return;
                }
                const auto& a = element.geometry->border_box;
                const auto& b = snapshot.elements[*other].geometry->border_box;
                const double width = std::min(right(a), right(b)) - std::max(a.origin.x, b.origin.x);
                const double height = std::min(bottom(a), bottom(b)) - std::max(a.origin.y, b.origin.y);
                passed = a.size.width == 0 || a.size.height == 0 || b.size.width == 0 || b.size.height == 0 ||
                    width <= predicate.tolerance || height <= predicate.tolerance;
            }
            result.status = passed ? InvariantStatus::passed : InvariantStatus::failed;
            if (!passed) diagnostic(result, "invariant_failed", path, "The captured observation does not satisfy invariant '" + result.declaration.name + "'; compare its declaration and observations.");
        }, result.declaration.predicate);
        report.results.push_back(std::move(result));
    }
    return {std::move(report), {}};
}
} // namespace tessera
