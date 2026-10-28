// Language: C++20; file: catalog_resolver.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "catalog_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace open_driver {
namespace {

std::string normalize(std::string value) {
    std::string result;
    result.reserve(value.size());
    bool pending_space = false;
    for (const unsigned char character : value) {
        if (std::isspace(character) || character == ',' || character == ';') {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(' ');
            pending_space = false;
        }
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

std::string normalize_architecture(std::string value) {
    value = normalize(std::move(value));
    if (value == "x64" || value == "amd64") return "amd64";
    if (value == "x86" || value == "i386") return "x86";
    if (value == "arm64" || value == "aarch64") return "arm64";
    return value;
}

bool contains_normalized(const std::vector<std::string>& values, const std::string& target) {
    const auto normalized_target = normalize(target);
    return std::any_of(values.begin(), values.end(), [&](const auto& value) {
        return normalize(value) == normalized_target;
    });
}

bool architecture_matches(
    const std::vector<std::string>& architectures,
    const std::string& target) {
    const auto normalized_target = normalize_architecture(target);
    return std::any_of(architectures.begin(), architectures.end(), [&](const auto& value) {
        const auto normalized = normalize_architecture(value);
        return normalized == normalized_target || normalized == "all" || normalized == "neutral";
    });
}

void add_score(
    CatalogResolutionCandidate& candidate,
    std::int32_t points,
    std::string reason) {
    candidate.score += points;
    candidate.decision_trace.push_back(
        "+" + std::to_string(points) + " " + std::move(reason));
}

}  // namespace

CatalogResolutionResult resolve_catalog_candidates(
    const CatalogResolutionTarget& target,
    const std::vector<CatalogSearchEntry>& search_entries,
    const std::vector<CatalogDetails>& details) {
    CatalogResolutionResult result;
    std::unordered_map<std::string, const CatalogSearchEntry*> search_by_id;
    for (const auto& entry : search_entries) search_by_id[entry.catalog_update_id] = &entry;

    for (const auto& detail : details) {
        const auto search = search_by_id.find(detail.catalog_update_id);
        if (search == search_by_id.end()) continue;
        CatalogResolutionCandidate candidate;
        candidate.search = *search->second;
        candidate.details = detail;
        candidate.eligible = true;

        if (!contains_normalized(detail.hardware_ids, target.hardware_id)) {
            candidate.eligible = false;
            candidate.rejection_reasons.push_back("hardware ID is not published by the package");
        } else {
            add_score(candidate, 100, "exact hardware ID");
        }
        if (!architecture_matches(detail.architectures, target.architecture)) {
            candidate.eligible = false;
            candidate.rejection_reasons.push_back("target architecture is not published by the package");
        } else {
            add_score(candidate, 60, "target architecture");
        }

        if (normalize(candidate.search.title) == normalize(target.title)) {
            add_score(candidate, 40, "exact catalog title");
        } else if (!detail.model.empty() &&
                   normalize(target.title).find(normalize(detail.model)) != std::string::npos) {
            add_score(candidate, 15, "driver model appears in the offer title");
        }
        if (!target.language.empty() && contains_normalized(detail.languages, target.language)) {
            add_score(candidate, 30, "system language");
        } else if (detail.languages.empty()) {
            add_score(candidate, 10, "language-neutral package");
        }
        if (target.driver_date_yyyymmdd != 0 &&
            detail.version_date_yyyymmdd == target.driver_date_yyyymmdd) {
            add_score(candidate, 20, "driver date");
        }
        if (!target.provider.empty() && normalize(detail.provider) == normalize(target.provider)) {
            add_score(candidate, 10, "driver provider");
        }
        if (!detail.model.empty() &&
            normalize(target.title).find(normalize(detail.model)) != std::string::npos) {
            add_score(candidate, 10, "driver model");
        }
        result.candidates.push_back(std::move(candidate));
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& left, const auto& right) {
        if (left.eligible != right.eligible) return left.eligible > right.eligible;
        if (left.score != right.score) return left.score > right.score;
        return left.search.catalog_update_id < right.search.catalog_update_id;
    });

    const auto eligible_count = std::count_if(
        result.candidates.begin(), result.candidates.end(),
        [](const auto& candidate) { return candidate.eligible; });
    if (eligible_count == 0) return result;
    const auto best_score = result.candidates.front().score;
    const auto tied = std::count_if(
        result.candidates.begin(), result.candidates.end(), [&](const auto& candidate) {
            return candidate.eligible && candidate.score == best_score;
        });
    result.ambiguous = tied > 1;
    if (!result.ambiguous) {
        result.selected_catalog_update_id = result.candidates.front().search.catalog_update_id;
    }
    return result;
}

}  // namespace open_driver
