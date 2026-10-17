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
        // Discrete choices use the same commands; capabilities are independent of telemetry/drafts.
        (void)take(host->invoke("msaa-4", CommandSource::accessibility, text));
        (void)take(host->msaa_capabilities({1, 2, 4, 8}, true, text));
        const auto msaa = take(host->request_value("msaa", 8, CommandSource::agent));
        const auto result = take(host->execute(msaa, text));
        if (result.outcome.status != CommandOutcomeStatus::success)
            throw std::runtime_error("MSAA setting rejected");
        std::cout << "MSAA " << host->msaa().accepted << "; supported choices " << host->msaa().supported.size() << '\n';
        std::cout << "Direct strength " << diagnostics::canonical(host->numeric().accepted) << "; draft " <<
            host->numeric().draft.text << "; setting attempts " << host->setting_attempts() << "; changes " <<
            host->setting_changes() << "; camera events " << host->camera_events() << '\n';
        for (const auto& node : host->view().inspection.semantics.nodes)
            if (node.id && (*node.id == "value-frame-summary" || *node.id == "history-summary" ||
                *node.id == "value-gpu-geometry" || *node.id == "disclosure-counters"))
                std::cout << *node.id << ": " << node.name << '\n';
        std::cout << "Focus " << host->focused().value_or("none") << "; offset " << host->offset().y <<
            "; disclosure requests " << host->disclosure_requests() << " (synthetic host; no window)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
