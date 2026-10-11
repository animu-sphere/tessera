#include <tessera/ui/keyed.hpp>
#include "json_detail.hpp"
#include <algorithm>
#include <limits>
#include <map>

namespace tessera {
namespace {
enum class Match { reuse, fresh, replace };

void check_name(std::vector<Diagnostic>& errors, std::string_view value, std::string path,
                std::string code, std::string_view what) {
    const bool controls = std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return c < 0x20 || c == 0x7f;
    });
    if (value.empty() || value.size() > 256 || controls || !detail::valid_utf8(value))
        errors.push_back({std::move(code), Severity::error, std::move(path),
            "Supply a nonempty UTF-8 " + std::string(what) + " of at most 256 bytes without control characters.", {}});
}
} // namespace

Result<KeyedUpdate> KeyedCollection::reconcile(ReactiveRuntime& runtime, const std::vector<KeyedChild>& children) {
    std::vector<Diagnostic> errors;
    if (children.size() > max_keyed_children) {
        errors.push_back({"keyed_child_limit", Severity::error, "/children",
                          "Supply at most 4096 keyed children in one collection.", {}});
        return {{}, std::move(errors)};
    }
    std::map<std::string_view, std::size_t, std::less<>> seen;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto path = "/children/" + std::to_string(i);
        const auto before = errors.size();
        check_name(errors, children[i].key, path + "/key", "invalid_key", "key");
        check_name(errors, children[i].kind, path + "/kind", "invalid_kind", "kind");
        if (errors.size() != before) continue;
        if (const auto [it, inserted] = seen.emplace(children[i].key, i); !inserted)
            errors.push_back({"duplicate_key", Severity::error, path + "/key",
                "Key '" + children[i].key + "' already names child " + std::to_string(it->second) +
                "; keys must be unique within one collection.", {}});
    }
    if (!errors.empty()) return {{}, std::move(errors)};

    std::map<std::string_view, std::size_t, std::less<>> previous;
    for (std::size_t j = 0; j < instances_.size(); ++j) previous.emplace(instances_[j].key, j);
    std::vector<Match> match(children.size(), Match::fresh);
    std::vector<bool> kept(instances_.size(), false);
    std::size_t creations = 0;
    // Build every record before entering the runtime, so that after runtime
    // operations succeed only owner/lifetime assignment and swaps remain.
    std::vector<KeyedInstance> next;
    next.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto it = previous.find(children[i].key);
        if (it != previous.end() && instances_[it->second].kind == children[i].kind) {
            match[i] = Match::reuse;
            kept[it->second] = true;
            next.push_back(instances_[it->second]);
            continue;
        }
        if (it != previous.end()) match[i] = Match::replace;
        ++creations;
        next.push_back({children[i].key, children[i].kind, 0, {}});
    }
    if (next_lifetime_ - 1 > std::numeric_limits<std::uint64_t>::max() - creations) {
        errors.push_back({"keyed_lifetime_exhausted", Severity::error, "/children",
                          "Create a new collection before instance lifetimes exhaust.", {}});
        return {{}, std::move(errors)};
    }
    KeyedUpdate update;
    update.created.reserve(creations);
    for (std::size_t j = 0; j < instances_.size(); ++j)
        if (!kept[j]) update.disposed.push_back(instances_[j]);

    // Fresh keys are created before any disposal, so a runtime failure there
    // leaves the collection unchanged. A later failure keeps the collection
    // truthful: it drops instances it disposed and disposes owners it created.
    std::vector<std::size_t> made;
    made.reserve(creations);
    std::vector<bool> closed(instances_.size(), false);
    const auto create = [&](Match kind) {
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (match[i] != kind) continue;
            next[i].owner = runtime.owner(scope_, children[i].key);
            made.push_back(i);
        }
    };
    try {
        create(Match::fresh);
        for (std::size_t j = 0; j < instances_.size(); ++j) {
            if (kept[j]) continue;
            instances_[j].owner.dispose();
            closed[j] = true;
        }
        create(Match::replace);
    } catch (...) {
        for (const auto i : made) next[i].owner.dispose();
        std::size_t kept_count = 0;
        for (std::size_t j = 0; j < instances_.size(); ++j)
            if (!closed[j]) instances_[kept_count++] = std::move(instances_[j]);
        instances_.resize(kept_count);
        throw;
    }
    for (std::size_t i = 0; i < next.size(); ++i) {
        if (match[i] == Match::reuse) continue;
        next[i].lifetime = next_lifetime_++;
        update.created.push_back(i);
    }
    instances_.swap(next);
    update.instances = instances_;
    return {std::move(update), {}};
}

const KeyedInstance* KeyedCollection::find(std::string_view key) const noexcept {
    for (const auto& instance : instances_)
        if (instance.key == key) return &instance;
    return nullptr;
}

bool KeyedCollection::current(std::string_view key, std::uint64_t lifetime) const noexcept {
    const auto* instance = find(key);
    return instance && instance->lifetime == lifetime;
}
} // namespace tessera
