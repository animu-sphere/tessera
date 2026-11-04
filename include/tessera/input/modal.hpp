#pragma once

#include <tessera/input/focus.hpp>
#include <utility>

namespace tessera {

struct ModalDispatchResult {
    bool active = false;
    bool dismiss = false; // Host closes the controlled value, then refreshes.
    bool consumed = false;
    bool reset_pointer = false;      // Cancel ordinary presses on open/close/dismiss.
    std::optional<NodeHandle> focus; // Apply in the supplied snapshot.
    bool operator==(const ModalDispatchResult&) const = default;
};

// Host-owned policy for one root Modal. Refresh closed views too, passing current
// focus to remember its stable author ID. Dispatch before ordinary event routing.
class ModalDispatcher final {
public:
    explicit ModalDispatcher(std::string id) : id_(std::move(id)) {}
    Result<ModalDispatchResult> refresh(const HitTestInput&, std::optional<NodeHandle> focused = {});
    Result<ModalDispatchResult> dispatch(const HitTestInput&, const InputEvent&,
                                         std::optional<NodeHandle> focused = {});

private:
    ModalDispatchResult update(const HitTestInput&, std::optional<NodeHandle>);
    std::string id_;
    std::optional<std::string> restore_;
    bool open_ = false;
    std::optional<std::chrono::microseconds> timestamp_;
};

} // namespace tessera
