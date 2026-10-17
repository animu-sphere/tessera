#pragma once

#include "panel.hpp"
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/paint.hpp>
#include <set>

namespace diagnostics {
struct View {
    tessera::CommandSnapshot commands;
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::LayoutResult layout;
    tessera::InspectionSnapshot inspection;
    tessera::UiDrawList paint;
    tessera::HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    tessera::SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
};

// A synchronous synthetic host. Returned preparation errors retain the accepted view.
// Keep the host and its borrowed text service on the creating UI thread.
class Host final {
public:
    static tessera::Result<std::unique_ptr<Host>> open(Snapshot, tessera::TextShaper&);
    const View& view() const { return *accepted_; }
    const Snapshot& snapshot() const { return snapshot_; }
    const Disclosure& disclosure() const { return disclosure_; }
    tessera::Point offset() const { return offset_; }
    std::optional<std::string> focused() const;
    std::size_t disclosure_requests() const { return disclosure_requests_; }
    const Numeric& numeric() const { return numeric_; }
    std::size_t setting_attempts() const { return setting_attempts_; }
    std::size_t setting_changes() const { return setting_changes_; }
    std::size_t draft_requests() const { return draft_requests_; }
    std::size_t camera_events() const { return camera_events_; }
    // Arbitration reports consumed errors separately from successful consumption.
    bool last_consumed() const { return last_consumed_; }
    tessera::Result<bool> route(const tessera::InputEvent&, tessera::CommandSource, tessera::TextShaper&);
    tessera::Result<bool> numeric_eligibility(bool, tessera::TextShaper&);
    tessera::Result<tessera::CommandInvocation> request_edit(const tessera::TextEditRequest&, tessera::CommandSource);
    tessera::Result<tessera::CommandInvocation> request_value(std::string field, double, tessera::CommandSource);

    tessera::Result<bool> refresh(Snapshot, tessera::TextShaper&);
    // Three eligibility flags are useful for host capability changes.
    tessera::Result<bool> eligibility(std::array<bool, 3>, tessera::TextShaper&);
    tessera::Result<bool> input(const tessera::InputEvent&, tessera::CommandSource, tessera::TextShaper&);
    tessera::Result<bool> invoke(const std::string& author_id, tessera::CommandSource, tessera::TextShaper&);
    tessera::Result<tessera::CommandInvocation> request(const std::string& author_id, tessera::CommandSource);
    tessera::Result<tessera::CommandExecution> execute(const tessera::CommandInvocation&, tessera::TextShaper&);

private:
    explicit Host(tessera::CommandRegistry registry) : registry_(std::move(registry)) {}
    tessera::Result<View> stage(const Snapshot&, const Disclosure&, tessera::Point,
        tessera::FocusDispatcher, bool reveal, std::array<bool, 3>, const Numeric&, tessera::TextShaper&);
    void accept(View, Snapshot, Disclosure, tessera::FocusDispatcher, std::array<bool, 3>, Numeric);
    tessera::Result<bool> operate(const tessera::ActionRequest&, tessera::CommandSource, tessera::TextShaper&);

    tessera::authoring::Session session_;
    tessera::CommandRegistry registry_;
    tessera::CommandExecutor executor_;
    Snapshot snapshot_;
    Disclosure disclosure_;
    Numeric numeric_;
    std::array<bool, 3> eligible_{true, true, true};
    tessera::Point offset_;
    tessera::FocusDispatcher focus_;
    tessera::PointerDispatcher pointers_;
    std::optional<View> accepted_;
    std::uint64_t generation_ = 0, sequence_ = 0;
    std::optional<std::chrono::microseconds> timestamp_;
    std::size_t disclosure_requests_ = 0;
    std::size_t setting_attempts_ = 0, setting_changes_ = 0, draft_requests_ = 0, camera_events_ = 0;
    bool last_consumed_ = false;
    std::set<std::uint32_t> claimed_, outside_;
};
} // namespace diagnostics
