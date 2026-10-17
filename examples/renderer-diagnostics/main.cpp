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
            (void)take(host->input({std::chrono::microseconds{++tick}, FocusNext{}}, CommandSource::keyboard, text));
        (void)take(host->input({std::chrono::microseconds{++tick}, Activate{}}, CommandSource::gamepad, text));
        // Snapshot injection never invokes a renderer command or samples a device.
        (void)take(host->refresh(diagnostics::sample_b(), text));
        (void)take(host->refresh(diagnostics::sample_c(), text));
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
