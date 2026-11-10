#include <tessera/input/popup.hpp>
#include "../detail/popup.hpp"
#include "../detail/targeting.hpp"
#include <tessera/semantics/semantics.hpp>
#include <algorithm>
#include <cmath>

namespace tessera {
namespace {

bool cancel(const InputEvent& event) {
    if (std::holds_alternative<Cancel>(event.data)) return true;
    const auto* key = std::get_if<KeyDown>(&event.data);
    return key && key->key == Key::escape && key->modifiers == Modifiers{};
}

bool descendant(const HitTestInput& input, NodeHandle node, NodeHandle ancestor) {
    const auto& boxes = input.layout->boxes;
    const auto found = std::find_if(boxes.begin(), boxes.end(), [&](const LayoutBox& box) { return box.node == node; });
    if (found == boxes.end()) return false;
    auto at = static_cast<std::uint32_t>(found - boxes.begin());
    while (at != no_layout_parent) {
        if (boxes[at].node == ancestor) return true;
        at = boxes[at].parent;
    }
    return false;
}

std::vector<Diagnostic> event_errors(const HitTestInput& input, const InputEvent& event,
                                     std::optional<std::chrono::microseconds> previous) {
    auto errors = validate(input);
    auto next = validate(event);
    errors.insert(errors.end(), next.begin(), next.end());
    if (previous && event.timestamp < *previous)
        errors.push_back({"event_order", Severity::error, "/timestamp",
                          "Send non-decreasing timestamps within this dispatcher's stream."});
    return errors;
}

std::optional<NodeHandle> anchor_focus(const HitTestInput& input, const std::optional<std::string>& id) {
    const auto node = id ? input.tree->find(*id) : std::nullopt;
    if (!node) return {};
    const detail::Targeting targeting(input);
    const auto at = targeting.box(*node);
    return at != no_layout_parent && targeting.focusable(at) ? node : std::nullopt;
}

void check_focus(std::vector<Diagnostic>& errors, const HitTestInput& input, std::optional<NodeHandle> focused) {
    if (input.tree && focused && !input.tree->get(*focused))
        errors.push_back({"stale_target", Severity::error, "/focused",
                          "Pass the focus handle for the current snapshot, not an expired tree."});
}

void check_id(std::vector<Diagnostic>& errors, const std::string& id) {
    if (id.empty())
        errors.push_back({"invalid_identifier", Severity::error, "/id", "Supply a nonempty stable author ID."});
}

void check_popup(std::vector<Diagnostic>& errors, const HitTestInput& input, const std::string& id) {
    check_id(errors, id);
    if (!input.tree) return;
    const auto node = input.tree->find(id);
    if (node && !detail::popup_anchor(*input.tree->get(*node)))
        errors.push_back({"popup_target", Severity::error, "/id", "Identify a Popup, not an ordinary content node."});
}

void check_menu(std::vector<Diagnostic>& errors, const HitTestInput& input, const std::string& id) {
    check_popup(errors, input, id);
    if (!input.tree) return;
    const auto node = input.tree->find(id);
    if (node && detail::collection_role(*input.tree->get(*node)) != "menu")
        errors.push_back({"menu_target", Severity::error, "/id", "Identify a Menu or ContextMenu."});
}

void check_combobox(std::vector<Diagnostic>& errors, const HitTestInput& input, const std::string& id) {
    check_id(errors, id);
    if (!input.tree) return;
    if (const auto node = input.tree->find(id); node && detail::collection_role(*input.tree->get(*node)) != "combobox")
        errors.push_back({"combobox_target", Severity::error, "/id", "Identify a ComboBox trigger."});
    check_popup(errors, input, id + "-list");
    if (const auto node = input.tree->find(id + "-list");
        node && detail::collection_role(*input.tree->get(*node)) != "listbox")
        errors.push_back({"combobox_target", Severity::error, "/id", "The ComboBox list must be a listbox."});
}

bool sequential(const InputEvent& event) {
    const auto* key = std::get_if<KeyDown>(&event.data);
    return (key && key->key == Key::tab && !key->modifiers.control && !key->modifiers.alt && !key->modifiers.super) ||
           std::holds_alternative<FocusNext>(event.data) || std::holds_alternative<FocusPrevious>(event.data);
}

// Pointer hover over an eligible item of the given role requests its focus.
std::optional<NodeHandle> hovered_item(const HitTestInput& input, const InputEvent& event, NodeHandle popup,
                                       std::string_view role) {
    const auto* move = std::get_if<PointerMove>(&event.data);
    if (!move) return {};
    const detail::Targeting targeting(input);
    const auto owner = targeting.binding_owner(targeting.hit(move->position));
    if (owner && descendant(input, *owner, popup) && detail::collection_role(*input.tree->get(*owner)) == role &&
        targeting.focusable(targeting.box(*owner)))
        return owner;
    return {};
}

MenuDispatchResult menu_result(const PopupDispatchResult& result) {
    return {result.dismiss, result.consumed, result.focus, {}};
}

} // namespace

PopupDispatchResult PopupDispatcher::update(const HitTestInput& input) {
    PopupDispatchResult result;
    const bool was_open = open_;
    const auto node = input.tree->find(id_);
    const auto* anchor = node ? detail::popup_anchor(*input.tree->get(*node)) : nullptr;
    const detail::Targeting targeting(input);
    const auto at = node ? targeting.box(*node) : no_layout_parent;
    const auto anchor_node = anchor ? input.tree->find(*anchor) : std::nullopt;
    const bool shown = anchor && at != no_layout_parent && targeting.eligible(*node) && anchor_node &&
                       targeting.eligible(*anchor_node);
    if (open_ && (!shown || !anchor_ || *anchor_ != *anchor)) {
        result.focus = anchor_focus(input, anchor_);
        open_ = false;
    }
    if (!shown) {
        result.dismiss = (was_open && !node) || (anchor && input.styles[node->index].display != Display::none);
        return result;
    }
    anchor_ = *anchor;
    if (!open_) {
        // The first eligible descendant, unless a selected listbox option is eligible.
        for (std::size_t i = 0; i < input.layout->boxes.size(); ++i) {
            const auto candidate = input.layout->boxes[i].node;
            if (!targeting.focusable(i) || !descendant(input, candidate, *node)) continue;
            const auto& item = *input.tree->get(candidate);
            const auto* selected = effective_property(item, property_names::selected);
            const bool chosen = detail::collection_role(item) == "option" && selected && std::get<bool>(*selected);
            if (!result.focus || chosen) result.focus = candidate;
            if (chosen) break;
        }
    }
    open_ = true;
    return result;
}

Result<PopupDispatchResult> PopupDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    check_popup(errors, input, id_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {update(input), {}};
}

Result<PopupDispatchResult> PopupDispatcher::dispatch(const HitTestInput& input, const InputEvent& event) {
    auto errors = event_errors(input, event, timestamp_);
    check_popup(errors, input, id_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto result = update(input);
    timestamp_ = event.timestamp;
    if (!open_) return {result, {}};
    bool dismiss = cancel(event);
    if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        const auto target = detail::Targeting(input).hit(press->position);
        const auto popup = *input.tree->find(id_);
        dismiss = !target || !descendant(input, *target, popup);
    }
    if (dismiss) {
        result.dismiss = result.consumed = true;
        result.focus = anchor_focus(input, anchor_);
        // Keep the open transition until the host publishes the closed view.
    }
    return {result, {}};
}

Result<MenuDispatchResult> MenuDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    check_menu(errors, input, id_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto result = popup_.refresh(input);
    if (!result) return {std::nullopt, std::move(result.diagnostics)};
    return {menu_result(*result.value), {}};
}

Result<MenuDispatchResult> MenuDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
                                                    std::optional<NodeHandle> focused) {
    auto errors = validate(input);
    check_menu(errors, input, id_);
    check_focus(errors, input, focused);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto popup = popup_.dispatch(input, event);
    if (!popup) return {std::nullopt, std::move(popup.diagnostics)};
    auto result = menu_result(*popup.value);
    const auto menu = input.tree->find(id_);
    if (!menu) return {result, {}};
    const auto& node = *input.tree->get(*menu);
    const auto* anchor_id = detail::popup_anchor(node);
    const auto anchor = anchor_id ? input.tree->find(*anchor_id) : std::nullopt;
    const detail::Targeting targeting(input);
    const bool shown = targeting.eligible(*menu);
    if (node.properties.contains(property_names::popup_x) && anchor && targeting.eligible(*anchor)) {
        const auto* box = detail::popup_anchor_box(*input.tree, *input.layout, node);
        if (box) {
            if (const auto* press = std::get_if<PointerDown>(&event.data);
                press && press->button == PointerButton::secondary) {
                const auto hit = targeting.hit(press->position);
                if (hit && descendant(input, *hit, *anchor)) result.open_at = press->position;
            }
            if (const auto* key = std::get_if<KeyDown>(&event.data);
                key && key->key == Key::f10 && key->modifiers == Modifiers{true} && !key->repeat && focused &&
                targeting.eligible(*focused) && descendant(input, *focused, *anchor)) {
                const auto visible = box->clip ? intersect(box->border_box, *box->clip) : box->border_box;
                result.open_at = Point{visible.origin.x, visible.origin.y + visible.size.height};
            }
        }
    }
    if (result.open_at) {
        result.dismiss = false;
        result.consumed = true;
        for (std::size_t i = 0; i < input.layout->boxes.size(); ++i) {
            const auto candidate = input.layout->boxes[i].node;
            if (targeting.focusable(i) && descendant(input, candidate, *menu)) {
                result.focus = candidate;
                break;
            }
        }
        return {result, {}};
    }
    if (!shown || result.dismiss) return {result, {}};
    if (sequential(event)) {
        result.dismiss = true;
        result.focus = anchor_focus(input, anchor_id ? std::optional(*anchor_id) : std::nullopt);
        // Publish the closed view before forwarding the sequential focus command.
        result.consumed = false;
    } else if (const auto item = hovered_item(input, event, *menu, "menu_item")) {
        result.focus = item;
    }
    return {result, {}};
}

Result<ComboBoxDispatchResult> ComboBoxDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    check_combobox(errors, input, id_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto result = popup_.refresh(input);
    if (!result) return {std::nullopt, std::move(result.diagnostics)};
    return {ComboBoxDispatchResult{result.value->dismiss, result.value->consumed, result.value->focus}, {}};
}

Result<ComboBoxDispatchResult> ComboBoxDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
                                                            std::optional<NodeHandle> focused) {
    auto errors = validate(input);
    check_combobox(errors, input, id_);
    check_focus(errors, input, focused);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto popup = popup_.dispatch(input, event);
    if (!popup) return {std::nullopt, std::move(popup.diagnostics)};
    ComboBoxDispatchResult result{popup.value->dismiss, popup.value->consumed, popup.value->focus};
    const auto list = input.tree->find(id_ + "-list");
    const auto trigger = input.tree->find(id_);
    const detail::Targeting targeting(input);
    const bool shown = list && targeting.box(*list) != no_layout_parent && targeting.eligible(*list) &&
                       detail::popup_anchor_box(*input.tree, *input.layout, *input.tree->get(*list));
    const auto* key = std::get_if<KeyDown>(&event.data);
    const bool alt = key && key->modifiers == Modifiers{.alt = true} && !key->repeat;
    if (!shown) {
        // Alt+Down on the focused, eligible trigger opens the list.
        if (alt && key->key == Key::down && trigger && focused == trigger &&
            targeting.box(*trigger) != no_layout_parent && targeting.focusable(targeting.box(*trigger)))
            result.open = result.consumed = true;
        return {result, {}};
    }
    if (result.dismiss) return {result, {}};
    if (alt && key->key == Key::up) {
        result.dismiss = result.consumed = true;
        result.focus = anchor_focus(input, id_);
    } else if (sequential(event)) {
        result.dismiss = true;
        result.focus = anchor_focus(input, id_);
        // Publish the closed view before forwarding the sequential focus command.
        result.consumed = false;
    } else if (const auto item = hovered_item(input, event, *list, "option")) {
        result.focus = item;
    }
    return {result, {}};
}

namespace {

void check_color_picker(std::vector<Diagnostic>& errors, const HitTestInput& input, const std::string& id) {
    check_id(errors, id);
    if (!input.tree) return;
    if (const auto node = input.tree->find(id);
        node && detail::collection_role(*input.tree->get(*node)) != "color_picker")
        errors.push_back({"color_picker_target", Severity::error, "/id", "Identify a ColorPicker trigger."});
    check_popup(errors, input, id + "-panel");
    if (const auto node = input.tree->find(id + "-panel");
        node && detail::collection_role(*input.tree->get(*node)) != "group")
        errors.push_back({"color_picker_target", Severity::error, "/id", "The ColorPicker panel must be a group."});
}

void check_color_input(std::vector<Diagnostic>& errors, const ColorPickerSteps& steps, const HsvColor& current) {
    const auto positive = [](double step) { return std::isfinite(step) && step > 0; };
    if (!positive(steps.hue) || !positive(steps.saturation) || !positive(steps.value))
        errors.push_back(
            {"color_picker_steps", Severity::error, "/steps", "Supply finite positive ColorPicker adjustment steps."});
    if (!valid(current))
        errors.push_back({"color_picker_value", Severity::error, "/current",
                          "Supply the accepted color: a hue in [0, 360] and saturation and value in [0, 1]."});
}

double rounded(double value, double scale) { return std::round(value * scale) / scale; }

// Clamped and rounded, so repeated steps and pointer positions produce reproducible values.
HsvColor normalized(HsvColor color) {
    return {rounded(std::clamp(color.hue, 0., 360.), 100), rounded(std::clamp(color.saturation, 0., 1.), 10000),
            rounded(std::clamp(color.value, 0., 1.), 10000)};
}

} // namespace

Result<ColorPickerDispatchResult> ColorPickerDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    check_color_picker(errors, input, id_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto result = popup_.refresh(input);
    if (!result) return {std::nullopt, std::move(result.diagnostics)};
    const auto panel = input.tree->find(id_ + "-panel");
    if (!panel || !detail::Targeting(input).eligible(*panel) || result.value->dismiss) reset();
    return {ColorPickerDispatchResult{result.value->dismiss, result.value->consumed, result.value->focus}, {}};
}

Result<ColorPickerDispatchResult> ColorPickerDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
                                                                  std::optional<NodeHandle> focused, HsvColor current) {
    auto errors = validate(input);
    check_color_picker(errors, input, id_);
    check_focus(errors, input, focused);
    check_color_input(errors, steps_, current);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto popup = popup_.dispatch(input, event);
    if (!popup) return {std::nullopt, std::move(popup.diagnostics)};
    ColorPickerDispatchResult result{popup.value->dismiss, popup.value->consumed, popup.value->focus};
    const auto panel = input.tree->find(id_ + "-panel");
    const auto trigger = input.tree->find(id_);
    const detail::Targeting targeting(input);
    const bool shown = panel && targeting.box(*panel) != no_layout_parent && targeting.eligible(*panel) &&
                       detail::popup_anchor_box(*input.tree, *input.layout, *input.tree->get(*panel));
    const auto* key = std::get_if<KeyDown>(&event.data);
    const bool alt = key && key->modifiers == Modifiers{.alt = true} && !key->repeat;
    if (!shown || result.dismiss) {
        reset();
        // Alt+Down on the focused, eligible trigger opens the panel.
        if (!shown && alt && key->key == Key::down && trigger && focused == trigger &&
            targeting.box(*trigger) != no_layout_parent && targeting.focusable(targeting.box(*trigger)))
            result.open = result.consumed = true;
        return {result, {}};
    }
    // Eligible panel controls in Tab order; a disabled picker has none.
    std::vector<NodeHandle> controls;
    for (const auto* suffix : {"-area", "-hue"})
        if (const auto node = input.tree->find(id_ + suffix);
            node && targeting.box(*node) != no_layout_parent && targeting.focusable(targeting.box(*node)))
            controls.push_back(*node);
    const auto area = input.tree->find(id_ + "-area");
    const auto hue = input.tree->find(id_ + "-hue");
    const auto usable = [&](std::optional<NodeHandle> node) {
        return node && std::find(controls.begin(), controls.end(), *node) != controls.end();
    };
    // Requests a changed color through the control's own command.
    const auto request = [&](NodeHandle control, HsvColor next) -> Result<ColorPickerDispatchResult> {
        next = normalized(next);
        result.consumed = true;
        if (next != current) {
            auto action = request_semantic_action({input.tree, input.styles, input.layout}, control, "activate");
            if (!action) return {std::nullopt, std::move(action.diagnostics)};
            result.color = next;
            result.action = std::move(*action.value);
        }
        return {result, {}};
    };
    const auto at = [&](bool strip, Point point) {
        const auto content = input.layout->boxes[targeting.box(strip ? *hue : *area)].content_box();
        const auto fraction = [](float position, float origin, float extent) {
            return extent > 0 ? std::clamp((static_cast<double>(position) - origin) / extent, 0., 1.) : 0.;
        };
        auto next = current;
        if (strip)
            next.hue = 360 * fraction(point.x, content.origin.x, content.size.width);
        else {
            next.saturation = fraction(point.x, content.origin.x, content.size.width);
            next.value = 1 - fraction(point.y, content.origin.y, content.size.height);
        }
        return next;
    };
    if (drag_ && !usable(drag_->hue ? hue : area)) reset();
    if (const auto* lost = std::get_if<PointerCancel>(&event.data)) {
        if (drag_ && drag_->pointer == lost->pointer) {
            reset();
            result.consumed = true;
        }
        return {result, {}};
    }
    if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        if (drag_ || press->button != PointerButton::primary) return {result, {}};
        const auto target = targeting.hit(press->position);
        if (!usable(target) || (target != area && target != hue)) return {result, {}};
        result.focus = target;
        auto pressed = request(*target, at(target == hue, press->position));
        // Capture only once the request succeeded, so a rejected dispatch leaves no drag behind.
        if (pressed) drag_ = Drag{press->pointer, target == hue};
        return pressed;
    }
    if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        if (!drag_ || drag_->pointer != move->pointer) return {result, {}};
        return request(drag_->hue ? *hue : *area, at(drag_->hue, move->position));
    }
    if (const auto* release = std::get_if<PointerUp>(&event.data)) {
        if (!drag_ || drag_->pointer != release->pointer || release->button != PointerButton::primary)
            return {result, {}};
        const bool strip = drag_->hue;
        auto released = request(strip ? *hue : *area, at(strip, release->position));
        if (released) reset();
        return released;
    }
    const bool inside = focused && panel && descendant(input, *focused, *panel);
    if (alt && key->key == Key::up) {
        result.dismiss = result.consumed = true;
        result.focus = anchor_focus(input, id_);
    } else if (sequential(event)) {
        const bool backward = std::holds_alternative<FocusPrevious>(event.data) || (key && key->modifiers.shift);
        const auto found = focused ? std::find(controls.begin(), controls.end(), *focused) : controls.end();
        const auto index = found - controls.begin();
        const auto next = backward ? index - 1 : index + 1;
        if (found != controls.end() && next >= 0 && next < static_cast<std::ptrdiff_t>(controls.size())) {
            result.focus = controls[next];
            result.consumed = true;
        } else {
            result.dismiss = true;
            result.focus = anchor_focus(input, id_);
            // Publish the closed view before forwarding the sequential focus command.
            result.consumed = false;
        }
    } else if (inside && (std::holds_alternative<Activate>(event.data) ||
                          (key && key->key == Key::enter && key->modifiers == Modifiers{} && !key->repeat))) {
        result.dismiss = result.consumed = true;
        result.focus = anchor_focus(input, id_);
    } else if (usable(focused) && (focused == area || focused == hue)) {
        // Arrow keys and gamepad directions; other keys stay with ordinary routing.
        std::optional<Direction> direction;
        if (key && key->modifiers == Modifiers{}) {
            if (key->key == Key::left) direction = Direction::left;
            if (key->key == Key::right) direction = Direction::right;
            if (key->key == Key::up) direction = Direction::up;
            if (key->key == Key::down) direction = Direction::down;
        }
        if (const auto* navigate = std::get_if<Navigate>(&event.data)) direction = navigate->direction;
        auto next = current;
        if (focused == hue) {
            if (key && key->modifiers == Modifiers{} && key->key == Key::home)
                return request(*hue, {0, current.saturation, current.value});
            if (key && key->modifiers == Modifiers{} && key->key == Key::end)
                return request(*hue, {360, current.saturation, current.value});
            if (!direction) return {result, {}};
            const bool lower = *direction == Direction::left || *direction == Direction::down;
            next.hue += lower ? -steps_.hue : steps_.hue;
            return request(*hue, next);
        }
        if (!direction) return {result, {}};
        if (*direction == Direction::left || *direction == Direction::right)
            next.saturation += *direction == Direction::left ? -steps_.saturation : steps_.saturation;
        else
            next.value += *direction == Direction::down ? -steps_.value : steps_.value;
        return request(*area, next);
    }
    return {result, {}};
}

TooltipDispatchResult TooltipDispatcher::update(const HitTestInput& input, std::optional<NodeHandle> focused) {
    const detail::Targeting targeting(input);
    const auto anchor = input.tree->find(anchor_);
    bool active = false;
    if (anchor && targeting.eligible(*anchor)) {
        const auto at = targeting.box(*anchor);
        const auto& box = input.layout->boxes[at];
        const auto visible = box.clip ? intersect(box.border_box, *box.clip) : box.border_box;
        if (visible.size.width > 0 && visible.size.height > 0) {
            const auto hit = pointer_ ? targeting.hit(*pointer_) : std::nullopt;
            active = (hit && descendant(input, *hit, *anchor)) ||
                     (focused && targeting.eligible(*focused) && descendant(input, *focused, *anchor));
        }
    }
    if (!active) suppressed_ = false;
    return {active && !suppressed_};
}

Result<TooltipDispatchResult> TooltipDispatcher::refresh(const HitTestInput& input, std::optional<NodeHandle> focused) {
    auto errors = validate(input);
    check_focus(errors, input, focused);
    check_id(errors, anchor_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {update(input, focused), {}};
}

Result<TooltipDispatchResult> TooltipDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
                                                          std::optional<NodeHandle> focused) {
    auto errors = event_errors(input, event, timestamp_);
    check_id(errors, anchor_);
    check_focus(errors, input, focused);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        pointer_ = move->position;
        pointer_id_ = move->pointer;
    } else if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        pointer_ = press->position;
        pointer_id_ = press->pointer;
        suppressed_ = true;
    } else if (const auto* lost = std::get_if<PointerCancel>(&event.data); lost && pointer_id_ == lost->pointer) {
        pointer_.reset();
        pointer_id_.reset();
    }
    if (cancel(event)) suppressed_ = true;
    timestamp_ = event.timestamp;
    return {update(input, focused), {}};
}

} // namespace tessera
