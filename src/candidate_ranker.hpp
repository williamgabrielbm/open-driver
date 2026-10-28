// Language: C++20; file: candidate_ranker.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace open_driver {

enum class MatchKind {
    hardware_id,
    compatible_id,
};

struct DriverVersion {
    std::array<std::uint16_t, 4> parts{};
};

struct Candidate {
    std::string package_id;
    std::string package_hash;
    std::string matching_id;
    MatchKind match_kind{MatchKind::compatible_id};

    // Rank supplied by the Windows compatibility/selection layer. Lower wins.
    std::uint32_t windows_rank{};

    // Canonical YYYYMMDD value from the INF DriverVer date. Higher is newer.
    std::uint32_t driver_date{};
    DriverVersion driver_version{};

    bool architecture_applicable{};
    bool os_applicable{};
    bool signature_valid{};
    bool blocked_by_windows{};

    std::string provider;
    std::string description;
    std::string inf_path;
    std::string section_name;
    std::string inf_hardware_id;
    std::vector<std::string> inf_compatible_ids;
    std::uint32_t driver_node_flags{};
    bool currently_installed{};
    bool inf_signed{};
    bool authenticode_signed{};
};

struct RankedCandidate {
    Candidate candidate;
    std::vector<std::string> decision_trace;
};

struct RejectedCandidate {
    Candidate candidate;
    std::vector<std::string> reasons;
};

struct RankingResult {
    std::vector<RankedCandidate> accepted;
    std::vector<RejectedCandidate> rejected;
};

[[nodiscard]] RankingResult rank_candidates(std::vector<Candidate> candidates);

}  // namespace open_driver
