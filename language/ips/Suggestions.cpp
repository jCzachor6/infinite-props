#include "ips/Suggestions.hpp"

#include <algorithm>
#include <numeric>
#include <utility>
#include <vector>

namespace iprop::ips {
namespace {
std::size_t distance(std::string_view a, std::string_view b) {
    // Three rows also recognize a common transposed pair, without a matrix
    // proportional to identifier length squared.
    std::vector<std::size_t> previous(b.size() + 1), older(b.size() + 1), current(b.size() + 1);
    std::iota(previous.begin(), previous.end(), 0);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            current[j] = std::min({current[j - 1] + 1, previous[j] + 1,
                                  previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                current[j] = std::min(current[j], older[j - 2] + 1);
        }
        older.swap(previous);
        previous.swap(current);
    }
    return previous.back();
}
}

std::string didYouMean(std::string_view misspelled, std::span<const std::string> candidates) {
    if (misspelled.size() < 3 || misspelled.size() > 128) return {};
    const std::size_t limit = misspelled.size() < 5 ? 1 : 2;
    std::size_t best = limit + 1;
    std::string candidate;
    for (const auto& name : candidates) {
        if (name == misspelled) return {};
        if (name.size() > misspelled.size() + limit || misspelled.size() > name.size() + limit) continue;
        const auto score = distance(misspelled, name);
        if (score < best || (score == best && name < candidate)) {
            best = score;
            candidate = name;
        }
    }
    return best <= limit ? "; did you mean '" + candidate + "'?" : "";
}

std::string formerBuiltinHint(std::string_view function) {
    constexpr std::pair<std::string_view, std::string_view> renamed[] = {
        {"noise", "noise.value"}, {"fault", "noise.fault"}, {"warp", "noise.warp"},
        {"fold", "vec2.fold"}, {"polar", "vec2.polar"}, {"fill", "dist.fill"},
        {"color.set.a", "color.set_a"}, {"stream_push", "stream.push"}, {"stream_pop", "stream.pop"},
    };
    for (const auto& [old, current] : renamed)
        if (function == old) return "; it was renamed to '" + std::string(current) + "'";
    if (function == "i32trunc") return "; it was removed, use int(x)";
    return {};
}

std::string formerArgumentHint(std::string_view function, std::string_view argument) {
    const auto renamed = [&]() -> std::string_view {
        if (function == "noise.mask" && argument == "kind") return "mode";
        if (function == "vec2.rot" && argument == "a") return "angle";
        if (function == "vec2.fold" && argument == "n") return "sectors";
        if (function == "dist.fill" && argument == "d") return "distance";
        if (function == "dist.fill" && argument == "w") return "width";
        if (function == "surface.paint" && argument == "image") return "source";
        // The constructors part.create and parts.from_regions take a `source`.
        if (function.starts_with("part.") && function != "part.create" && argument == "p") return "part";
        if (function.starts_with("regions.") && (argument == "rs" || argument == "source")) return "regions";
        if (function.starts_with("diagram.") && argument == "d") return "diagram";
        if ((function == "parts.count" || function == "parts.get") && argument == "source") return "parts";
        return {};
    }();
    return renamed.empty() ? std::string() : "; it was renamed to '" + std::string(renamed) + "'";
}

} // namespace iprop::ips
