#include <tessera/input/focus.hpp>
#include <tessera/render/paint.hpp>
#include <iostream>
#include <string>
#include <utility>

namespace {

tessera::UiNode button(std::string id, std::string caption, std::string action) {
    tessera::UiNode label;
    label.kind = tessera::NodeKind::text;
    label.id = id + "-label";
    label.properties["text"] = std::move(caption);
    tessera::UiNode result;
    result.id = std::move(id);
    result.events["activate"] = std::move(action);
    result.properties["focusable"] = true;
    result.children.push_back(std::move(label));
    return result;
}

} // namespace

int main() {
    tessera::UiDocument document;
    document.root.id = "menu";
    document.root.children = {button("start", "Start", "start-game"), button("quit", "Quit", "quit-game")};
    const auto created = tessera::UiTree::create(document, {{"start-game", "quit-game"}});
    if (!created) return 1;
    const auto& tree = **created.value;
    std::vector<tessera::ResolvedStyle> styles(tree.size());
    styles[0].padding = {12, 12, 12, 12};
    styles[0].gap = 8;
    styles[0].background = {0.05f, 0.05f, 0.05f, 1};
    for (const char* id : {"start", "quit"}) {
        auto& style = styles[tree.find(id)->index];
        style.padding = {8, 8, 8, 8};
        style.background = {0.2f, 0.3f, 0.5f, 1};
        style.align = tessera::Align::center;
    }
    tessera::PlaceholderTextShaper text;
    const auto layout = tessera::compute_layout({&tree, styles, {200, 120}, &text});
    if (!layout) return 2;
    const auto paint = tessera::build_paint_list({&tree, styles, &*layout.value, &text});
    if (!paint || !tessera::validate(*paint.value).empty()) return 3;
    std::cout << "Generated " << paint.value->commands.size() << " paint commands with placeholder glyphs.\n";

    // The host supplies normalized synthetic pointer events and consumes owned action requests.
    // A native host would normalize OS input and execute its application actions at this boundary.
    tessera::PointerDispatcher input;
    const tessera::HitTestInput snapshot{&tree, styles, &*layout.value};
    std::chrono::microseconds time{0};
    std::vector<tessera::ActionRequest> clicked;
    for (const auto& box : layout.value->boxes) {
        const auto& node = *tree.get(box.node);
        if (node.kind != tessera::NodeKind::text) continue;
        const auto rect = box.content_box();
        const tessera::Point point{rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
        time += std::chrono::microseconds(1);
        const auto press = input.dispatch(snapshot, {time, tessera::PointerDown{{1}, point}});
        if (!press || press.value->target != box.node || !press.value->actions.empty()) return 4;
        time += std::chrono::microseconds(1);
        const auto release = input.dispatch(snapshot, {time, tessera::PointerUp{{1}, point}});
        if (!release || release.value->actions.size() != 1) return 5;
        const auto& action = release.value->actions[0];
        const auto* owner = tree.get(action.target);
        if (!owner || !owner->id || (action.action != "start-game" && action.action != "quit-game")) return 6;
        // Host application work belongs here, after dispatch has returned.
        std::cout << "Host received " << action.binding << ": " << action.action << " from " << *owner->id << '\n';
        clicked.push_back(action);
    }

    // Keyboard/gamepad hosts translate keys or buttons into logical commands; the same requests result.
    tessera::FocusDispatcher focus;
    for (const auto& expected : clicked) {
        time += std::chrono::microseconds(1);
        const auto moved = focus.dispatch(snapshot, {time, tessera::Navigate{tessera::Direction::down}});
        if (!moved || moved.value->focused != expected.target) return 7;
        time += std::chrono::microseconds(1);
        const auto activated = focus.dispatch(snapshot, {time, tessera::Activate{}});
        if (!activated || activated.value->actions != std::vector{expected}) return 8;
        std::cout << "Focus activation matched " << expected.action << '\n';
    }
    std::cout << "Synthetic pointer and focus menu completed. No native window, GPU, or device input is used.\n";
}
