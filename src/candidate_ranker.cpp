// Language: C++20; file: candidate_ranker.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace open_driver {
namespace {

std::string format_version(const DriverVersion& version) {
    std::ostringstream stream;
    stream << version.parts[0] << '.' << version.parts[1] << '.'
           << version.parts[2] << '.' << version.parts[3];
    return stream.str();
}

bool version_is_newer(const DriverVersion& left, const DriverVersion& right) {
    return left.parts > right.parts;
}

bool candidate_precedes(const RankedCandidate& left, const RankedCandidate& right) {
    const auto& a = left.candidate;
    const auto& b = right.candidate;

    if (a.windows_rank != b.windows_rank) {
        return a.windows_rank < b.windows_rank;
    }
    if (a.driver_date != b.driver_date) {
        return a.driver_date > b.driver_date;
    }
    if (a.driver_version.parts != b.driver_version.parts) {
        return version_is_newer(a.driver_version, b.driver_version);
    }
    if (a.package_hash != b.package_hash) {
        return a.package_hash < b.package_hash;
    }
    return a.package_id < b.package_id;
}

std::vector<std::string> rejection_reasons(const Candidate& candidate) {
    std::vector<std::string> reasons;
    if (!candidate.architecture_applicable) {
        reasons.emplace_back("architecture does not match the running Windows installation");
    }
    if (!candidate.os_applicable) {
        reasons.emplace_back("INF target operating-system decoration is not applicable");
    }
    if (!candidate.signature_valid) {
        reasons.emplace_back("package signature is not valid");
    }
    if (candidate.blocked_by_windows) {
        reasons.emplace_back("Windows marked the driver node as unusable");
    }
    return reasons;
}

std::vector<std::string> decision_trace(const Candidate& candidate) {
    return {
        "accepted: architecture, operating-system target, and signature are applicable",
        "matched by " + std::string(candidate.match_kind == MatchKind::hardware_id
                                         ? "hardware ID "
                                         : "compatible ID ") +
            candidate.matching_id,
        "Windows rank=" + std::to_string(candidate.windows_rank) + " (lower is better)",
        "INF date=" + std::to_string(candidate.driver_date),
        "INF version=" + format_version(candidate.driver_version),
        "final deterministic tie-break=" + candidate.package_hash + "/" +
            candidate.package_id,
    };
}

}  // namespace

RankingResult rank_candidates(std::vector<Candidate> candidates) {
    RankingResult result;
    result.accepted.reserve(candidates.size());
    result.rejected.reserve(candidates.size());

    for (auto& candidate : candidates) {
        auto reasons = rejection_reasons(candidate);
        if (!reasons.empty()) {
            result.rejected.push_back({std::move(candidate), std::move(reasons)});
            continue;
        }

        auto trace = decision_trace(candidate);
        result.accepted.push_back({std::move(candidate), std::move(trace)});
    }

    std::sort(result.accepted.begin(), result.accepted.end(), candidate_precedes);
    return result;
}

}  // namespace open_driver
