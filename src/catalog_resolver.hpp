// Language: C++20; file: catalog_resolver.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "microsoft_catalog.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace open_driver {

struct CatalogResolutionTarget {
    std::string title;
    std::string hardware_id;
    std::string architecture;
    std::string language;
    std::string provider;
    std::uint32_t driver_date_yyyymmdd{};
};

struct CatalogResolutionCandidate {
    CatalogSearchEntry search;
    CatalogDetails details;
    bool eligible{};
    std::int32_t score{};
    std::vector<std::string> decision_trace;
    std::vector<std::string> rejection_reasons;
};

struct CatalogResolutionResult {
    std::vector<CatalogResolutionCandidate> candidates;
    std::optional<std::string> selected_catalog_update_id;
    bool ambiguous{};
};

[[nodiscard]] CatalogResolutionResult resolve_catalog_candidates(
    const CatalogResolutionTarget& target,
    const std::vector<CatalogSearchEntry>& search_entries,
    const std::vector<CatalogDetails>& details);

}  // namespace open_driver
