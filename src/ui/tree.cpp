#include <tessera/ui/tree.hpp>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace tessera {
namespace {
std::atomic<std::uint64_t> next_identity{1};
std::uint64_t allocate_identity() {
    auto current = next_identity.load(std::memory_order_relaxed);
    for (;;) {
        if (current == 0) throw std::overflow_error("UiTree identity space exhausted");
        if (next_identity.compare_exchange_weak(current, current + 1,
                                               std::memory_order_relaxed)) return current;
    }
}
}

UiTree::UiTree(UiDocument document)
    : document_(std::move(document)), identity_(allocate_identity()) {
    index_node(document_.root);
}

std::uint32_t UiTree::index_node(const UiNode& node) {
    const auto index = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(&node);
    children_.emplace_back();
    if (node.id) ids_.emplace(*node.id, index);
    for (const auto& child : node.children) {
        const auto child_index = index_node(child);
        children_[index].push_back(child_index);
    }
    return index;
}

Result<std::unique_ptr<UiTree>> UiTree::create(const UiDocument& document, const ValidationContext& context) {
    auto diagnostics = validate(document, context);
    if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
    return {std::unique_ptr<UiTree>(new UiTree(document)), {}};
}

const UiNode* UiTree::get(NodeHandle handle) const noexcept {
    if (handle.tree != identity_ || handle.index >= nodes_.size()) return nullptr;
    return nodes_[handle.index];
}

std::vector<NodeHandle> UiTree::children(NodeHandle handle) const {
    std::vector<NodeHandle> result;
    if (get(handle)) {
        for (auto index : children_[handle.index]) result.push_back({identity_, index});
    }
    return result;
}

std::optional<NodeHandle> UiTree::find(std::string_view id) const {
    const auto found = ids_.find(id);
    if (found == ids_.end()) return std::nullopt;
    return NodeHandle{identity_, found->second};
}
} // namespace tessera
