#pragma once

#include <tessera/render/paint.hpp>
#include <functional>
#include <string_view>

namespace tessera::examples {

// Observers borrow a settled example frame for the duration of the call.
using FrameObserver = std::function<void(std::string_view, Size, const UiDrawList&)>;

int run_viewport_toolbar(const FrameObserver&);
int run_grid_inspector(const FrameObserver&);
int run_popup_tooltip(const FrameObserver&);
int run_modal_dialog(const FrameObserver&);
int run_tree_list(const FrameObserver&);
int run_menu_context(const FrameObserver&);
int run_settings_starter(const FrameObserver&);

} // namespace tessera::examples
