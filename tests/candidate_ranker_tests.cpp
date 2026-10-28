// Language: C++20; file: candidate_ranker_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using open_driver::Candidate;
using open_driver::DriverVersion;
using open_driver::MatchKind;

Candidate make_candidate(
    std::string package_id,
    std::uint32_t rank,
    std::uint32_t date,
    DriverVersion version,
    std::string hash = "hash") {
    return Candidate{
        .package_id = std::move(package_id),
        .package_hash = std::move(hash),
        .matching_id = "PCI\\VEN_1234&DEV_5678",
        .match_kind = MatchKind::hardware_id,
        .windows_rank = rank,
        .driver_date = date,
        .driver_version = version,
        .architecture_applicable = true,
        .os_applicable = true,
        .signature_valid = true,
    };
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void lower_rank_beats_newer_version() {
    auto better_rank = make_candidate("better-rank", 0x1000, 20240101, {{1, 0, 0, 0}});
    auto newer_version = make_candidate("newer-version", 0x2000, 20260101, {{99, 0, 0, 0}});

    const auto result = open_driver::rank_candidates({newer_version, better_rank});
    require(result.accepted.front().candidate.package_id == "better-rank",
            "rank must take precedence over date and version");
}

void date_then_version_break_equal_rank() {
    auto older = make_candidate("older", 0x1000, 20240101, {{9, 0, 0, 0}});
    auto newer_date = make_candidate("newer-date", 0x1000, 20250101, {{1, 0, 0, 0}});
    auto newer_version = make_candidate("newer-version", 0x1000, 20250101, {{2, 0, 0, 0}});

    const auto result = open_driver::rank_candidates({older, newer_date, newer_version});
    require(result.accepted[0].candidate.package_id == "newer-version",
            "version must break an equal-rank, equal-date tie");
    require(result.accepted[1].candidate.package_id == "newer-date",
            "newer date must precede an older date");
}

void inapplicable_packages_are_rejected_with_all_reasons() {
    auto candidate = make_candidate("bad-target", 0, 20250101, {{1, 0, 0, 0}});
    candidate.architecture_applicable = false;
    candidate.os_applicable = false;
    candidate.signature_valid = false;

    const auto result = open_driver::rank_candidates({candidate});
    require(result.accepted.empty(), "inapplicable package must not be ranked");
    require(result.rejected.size() == 1, "inapplicable package must be reported once");
    require(result.rejected[0].reasons.size() == 3,
            "every independent rejection reason must be retained");
}

void final_tie_break_is_deterministic() {
    auto z = make_candidate("z", 0x1000, 20250101, {{1, 0, 0, 0}}, "bbb");
    auto a = make_candidate("a", 0x1000, 20250101, {{1, 0, 0, 0}}, "aaa");

    const auto result = open_driver::rank_candidates({z, a});
    require(result.accepted[0].candidate.package_id == "a",
            "package hash must provide a stable final ordering");
}

}  // namespace

int main() {
    lower_rank_beats_newer_version();
    date_then_version_break_equal_rank();
    inapplicable_packages_are_rejected_with_all_reasons();
    final_tie_break_is_deterministic();
    std::cout << "candidate_ranker_tests: all checks passed\n";
    return 0;
}
