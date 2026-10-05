#pragma once

#include <tessera/input/pointer.hpp>
#include <optional>
#include <string>

namespace tessera {

struct FocusDispatchResult {
    std::optional<NodeHandle> focused; // Belongs to the supplied tree.
    std::vector<ActionRequest> actions;
    bool operator==(const FocusDispatchResult&) const = default;
};

// Host-owned, single-stream focus state for logical commands. Stores the focused handle and its author
// ID only, never callbacks or borrowed inputs. A node can take focus when it is displayed, locally
// visible, not disabled by itself or an ancestor, and its `focusable` property is true.
class FocusDispatcher final {
public:
    // Accepts only FocusNext/FocusPrevious/Navigate/Activate/Cancel; hosts translate keys and gamepad
    // input. Invalid input leaves state unchanged. When the focused node has become ineligible, the
    // call recovers focus and consumes the command.
    Result<FocusDispatchResult> dispatch(const HitTestInput&, const InputEvent&);
    // Recover focus after snapshot changes without moving it further or requesting an action. A new
    // tree identity restores focus by author ID.
    Result<FocusDispatchResult> refresh(const HitTestInput&);
    // Focus an eligible target in the supplied snapshot, e.g. for host pointer-press policy.
    Result<FocusDispatchResult> focus(const HitTestInput&, NodeHandle target);
    void reset() noexcept;

private:
    std::optional<NodeHandle> focused_;
    std::optional<std::string> id_; // Author ID of the focused node, when present.
    std::uint64_t tree_ = 0;
    std::optional<std::chrono::microseconds> timestamp_;
};

} // namespace tessera
