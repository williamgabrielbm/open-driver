// Language: C++20; file: catalog_resolver_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "catalog_resolver.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

open_driver::CatalogDetails details(
    std::string id,
    std::vector<std::string> architectures,
    std::vector<std::string> languages) {
    return {
        .catalog_update_id = std::move(id),
        .architectures = std::move(architectures),
        .languages = std::move(languages),
        .model = "HP V206hz LED Backlit Monitor",
        .provider = "HP",
        .version = "1.0.0.1",
        .version_date_yyyymmdd = 20131118,
        .hardware_ids = {"monitor\\hwp3138", "monitor\\hwp3139"},
    };
}

}  // namespace

int main() {
    const std::vector<open_driver::CatalogSearchEntry> entries{
        {.catalog_update_id = "english", .title = "HP - LCD - HP V206hz LED Backlit Monitor"},
        {.catalog_update_id = "ptbr", .title = "HP - LCD - HP V206hz LED Backlit Monitor"},
        {.catalog_update_id = "x86", .title = "HP - LCD - HP V206hz LED Backlit Monitor"},
    };
    const std::vector<open_driver::CatalogDetails> all_details{
        details("english", {"AMD64", "X86"}, {"English"}),
        details("ptbr", {"AMD64"}, {"Portuguese (Brazil)"}),
        details("x86", {"X86"}, {"Portuguese (Brazil)"}),
    };
    const open_driver::CatalogResolutionTarget target{
        .title = "HP - LCD - HP V206hz LED Backlit Monitor",
        .hardware_id = "MONITOR\\HWP3138",
        .architecture = "AMD64",
        .language = "Portuguese (Brazil)",
        .provider = "HP",
        .driver_date_yyyymmdd = 20131118,
    };
    const auto result = open_driver::resolve_catalog_candidates(target, entries, all_details);
    require(result.selected_catalog_update_id == std::optional<std::string>{"ptbr"},
            "locale-matching AMD64 package must win");
    require(!result.ambiguous, "winner must be unambiguous");
    require(result.candidates.size() == 3, "all detailed candidates must remain auditable");
    require(!result.candidates.back().eligible, "x86-only package must be rejected on AMD64");

    auto tied_details = all_details;
    tied_details[0].languages = {"Portuguese (Brazil)"};
    const auto tied = open_driver::resolve_catalog_candidates(target, entries, tied_details);
    require(tied.ambiguous, "equal best scores must remain ambiguous");
    require(!tied.selected_catalog_update_id, "ambiguous result must not select a package");
    std::cout << "catalog_resolver_tests: passed\n";
    return 0;
}
