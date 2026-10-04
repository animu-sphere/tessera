#pragma once

#include <tessera/ui/document.hpp>
#include <memory>
#include <string_view>

namespace tessera {

struct NodeHandle {
    std::uint64_t tree = 0;
    std::uint32_t index = 0;
    bool operator==(const NodeHandle&) const = default;
};

// Immutable owned snapshot. Handles belong to this instance and expire with it.
class UiTree final {
public:
    static Result<std::unique_ptr<UiTree>> create(
        const UiDocument& document, const ValidationContext& = {});
    UiTree(const UiTree&) = delete;
    UiTree& operator=(const UiTree&) = delete;
    UiTree(UiTree&&) = delete;
    UiTree& operator=(UiTree&&) = delete;

    const UiDocument& document() const noexcept { return document_; }
    NodeHandle root() const noexcept { return {identity_, 0}; }
    const UiNode* get(NodeHandle) const noexcept;
    std::vector<NodeHandle> children(NodeHandle) const;
    std::optional<NodeHandle> find(std::string_view author_id) const;
    std::size_t size() const noexcept { return nodes_.size(); }

private:
    explicit UiTree(UiDocument);
    std::uint32_t index_node(const UiNode&);
    UiDocument document_;
    std::uint64_t identity_;
    std::vector<const UiNode*> nodes_;
    std::vector<std::vector<std::uint32_t>> children_;
    std::map<std::string, std::uint32_t, std::less<>> ids_;
};

} // namespace tessera
