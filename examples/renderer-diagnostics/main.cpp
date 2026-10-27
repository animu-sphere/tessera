#include "host.hpp"
#include "sample_data.hpp"
#include <iostream>
#include <stdexcept>

namespace {
template<class T> T take(tessera::Result<T> result) {
    if (!result) {
        std::string message;
        for (const auto& error : result.diagnostics)
            message += error.code + " at " + error.path + ": " + error.message + "\n";
        throw std::runtime_error(message);
    }
    return std::move(*result.value);
}
}

int main() {
    try {
        using namespace tessera;
        PlaceholderTextShaper text;
        auto host = take(diagnostics::Host::open(diagnostics::sample_a(), text));
        // The platform adapter supplies normalized events and a monotonic clock.
        std::int64_t tick = 0;
        for (int i = 0; i < 3; ++i)
            (void)take(host->route({std::chrono::microseconds{++tick}, FocusNext{}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, Activate{}}, CommandSource::gamepad, text));
        // Snapshot injection never invokes a renderer command or samples a device.
        (void)take(host->refresh(diagnostics::sample_b(), text));
        (void)take(host->refresh(diagnostics::sample_c(), text));
        // Editing is routed before navigation; telemetry refresh retains the pending draft.
        for (int i = 0; i < 2; ++i)
            (void)take(host->route({std::chrono::microseconds{++tick}, FocusNext{}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::home}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::end, {.shift = true}}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, tessera::TextInput{"1.5"}}, CommandSource::keyboard, text));
        (void)take(host->refresh(diagnostics::sample_c(), text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::enter}}, CommandSource::keyboard, text));
        // Navigate past Decrease to Increase; all sources use the shared executor.
        for (int i = 0; i < 2; ++i)
            (void)take(host->route({std::chrono::microseconds{++tick}, FocusNext{}}, CommandSource::gamepad, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, Activate{}}, CommandSource::gamepad, text));
        (void)take(host->invoke("direct-decrease", CommandSource::agent, text));
        // A second numeric field owns its own draft and routes the same normalized keys.
        (void)take(host->invoke("ambient-input", CommandSource::agent, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::home}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::end, {.shift = true}}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, tessera::TextInput{"0.75"}}, CommandSource::keyboard, text));
        (void)take(host->refresh(diagnostics::sample_c(), text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::enter}}, CommandSource::keyboard, text));
        for (int i = 0; i < 2; ++i)
            (void)take(host->route({std::chrono::microseconds{++tick}, FocusNext{}}, CommandSource::gamepad, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, Activate{}}, CommandSource::gamepad, text));
        (void)take(host->invoke("ambient-decrease", CommandSource::accessibility, text));
        // Discrete choices use the same commands; capabilities are independent of telemetry/drafts.
        (void)take(host->invoke("msaa-4", CommandSource::accessibility, text));
        (void)take(host->msaa_capabilities({1, 2, 4, 8}, true, text));
        const auto msaa = take(host->request_value("msaa", 8, CommandSource::agent));
        const auto result = take(host->execute(msaa, text));
        if (result.outcome.status != CommandOutcomeStatus::success)
            throw std::runtime_error("MSAA setting rejected");
        // Controlled Checkbox activation and an exact Boolean set share that executor.
        (void)take(host->invoke("outlines-toggle", CommandSource::accessibility, text));
        const auto outlines = take(host->request_outlines("outlines", true, CommandSource::agent));
        if (take(host->execute(outlines, text)).outcome.status != CommandOutcomeStatus::success)
            throw std::runtime_error("Outlines setting rejected");
        (void)take(host->invoke("scene-lights-toggle", CommandSource::accessibility, text));
        const auto scene_lights = take(host->request_scene_lights("scene_lights", false, CommandSource::agent));
        if (take(host->execute(scene_lights, text)).outcome.status != CommandOutcomeStatus::success)
            throw std::runtime_error("Scene lights setting rejected");
        (void)take(host->invoke("material-view-normals", CommandSource::accessibility, text));
        const auto material_view = take(host->request_material_view("material_view", "base-color", CommandSource::agent));
        if (take(host->execute(material_view, text)).outcome.status != CommandOutcomeStatus::success)
            throw std::runtime_error("Material view setting rejected");
        // Key direction components are independent numeric fields in [-1,1]; the host does not normalize the vector.
        (void)take(host->invoke("key-z-increase", CommandSource::accessibility, text));
        (void)take(host->invoke("key-x-input", CommandSource::agent, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::home}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::end, {.shift = true}}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, tessera::TextInput{"-0.5"}}, CommandSource::keyboard, text));
        (void)take(host->route({std::chrono::microseconds{++tick}, KeyDown{Key::enter}}, CommandSource::keyboard, text));
        const auto key_y = take(host->request_value("key_y", 0.25, CommandSource::agent));
        if (take(host->execute(key_y, text)).outcome.status != CommandOutcomeStatus::success)
            throw std::runtime_error("Key direction setting rejected");
        (void)take(host->refresh(diagnostics::sample_c(), text)); // Retains the accepted choice without a setting request.
        const auto& key = host->key_direction();
        std::cout << "Key direction " << diagnostics::canonical(key.x.accepted) << ", " << diagnostics::canonical(key.y.accepted) <<
            ", " << diagnostics::canonical(key.z.accepted) << '\n';
        std::cout << "Material view " << host->material_view().accepted << '\n';
        std::cout << "Scene lights " << (host->scene_lights().accepted ? "on" : "off") << '\n';
        std::cout << "Outlines " << (host->outlines().accepted ? "on" : "off") << '\n';
        std::cout << "MSAA " << host->msaa().accepted << "; supported choices " << host->msaa().supported.size() << '\n';
        std::cout << "Ambient strength " << diagnostics::canonical(host->ambient().accepted) << "; draft " << host->ambient().draft.text << '\n';
        std::cout << "Direct strength " << diagnostics::canonical(host->numeric().accepted) << "; draft " <<
            host->numeric().draft.text << "; setting attempts " << host->setting_attempts() << "; changes " <<
            host->setting_changes() << "; camera events " << host->camera_events() << '\n';
        for (const auto& node : host->view().inspection.semantics.nodes)
            if (node.id && (*node.id == "value-frame-summary" || *node.id == "history-summary" ||
                *node.id == "value-gpu-geometry" || *node.id == "disclosure-counters"))
                std::cout << *node.id << ": " << node.name << '\n';
        // One command proposes every initial setting and canonical draft; capabilities and eligibility stay host-owned.
        (void)take(host->invoke("controls-reset", CommandSource::accessibility, text));
        const auto& reset = host->key_direction();
        std::cout << "Reset direct " << diagnostics::canonical(host->numeric().accepted) << "; ambient " <<
            diagnostics::canonical(host->ambient().accepted) << "; key direction " << diagnostics::canonical(reset.x.accepted) << ", " <<
            diagnostics::canonical(reset.y.accepted) << ", " << diagnostics::canonical(reset.z.accepted) << "; MSAA " << host->msaa().accepted <<
            "; outlines " << (host->outlines().accepted ? "on" : "off") << "; scene lights " << (host->scene_lights().accepted ? "on" : "off") <<
            "; material view " << host->material_view().accepted << "; setting attempts " << host->setting_attempts() << "; changes " <<
            host->setting_changes() << '\n';
        std::cout << "Focus " << host->focused().value_or("none") << "; offset " << host->offset().y <<
            "; disclosure requests " << host->disclosure_requests() << " (synthetic host; no window)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
