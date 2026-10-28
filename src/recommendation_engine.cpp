// Language: C++20; file: recommendation_engine.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "recommendation_engine.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace open_driver {
namespace {

std::string normalize(std::string value) {
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base(), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool exact_hardware_match(const DeviceRecord& device, const Candidate& candidate) {
    const auto matching = normalize(candidate.matching_id);
    return candidate.match_kind == MatchKind::hardware_id &&
           std::any_of(device.hardware_ids.begin(), device.hardware_ids.end(), [&](const auto& id) {
               return normalize(id) == matching;
           });
}

bool installed_match_is_compatible(const DeviceRecord& device) {
    const auto installed = normalize(device.installed_driver.matching_device_id);
    if (installed.empty()) return false;
    const bool hardware = std::any_of(device.hardware_ids.begin(), device.hardware_ids.end(),
        [&](const auto& id) { return normalize(id) == installed; });
    const bool compatible = std::any_of(device.compatible_ids.begin(), device.compatible_ids.end(),
        [&](const auto& id) { return normalize(id) == installed; });
    return !hardware && compatible;
}

RecommendationResult result(
    RecommendationDecision decision,
    std::vector<std::string> trace) {
    trace.push_back("decision=" + recommendation_decision_name(decision));
    return {.decision = decision, .decision_trace = std::move(trace)};
}

}  // namespace

std::optional<DriverVersion> parse_driver_version(const std::string& value) {
    DriverVersion version;
    std::size_t start = 0;
    for (std::size_t index = 0; index < version.parts.size(); ++index) {
        if (start >= value.size()) return std::nullopt;
        const auto separator = value.find('.', start);
        const auto end = separator == std::string::npos ? value.size() : separator;
        unsigned int part = 0;
        const auto parsed = std::from_chars(value.data() + start, value.data() + end, part);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + end || part > 65535U) {
            return std::nullopt;
        }
        version.parts[index] = static_cast<std::uint16_t>(part);
        if (index + 1 < version.parts.size()) {
            if (separator == std::string::npos) return std::nullopt;
            start = separator + 1;
        } else if (separator != std::string::npos) {
            return std::nullopt;
        }
    }
    return version;
}

bool driver_release_is_newer(
    const std::string& installed_version,
    std::uint32_t installed_date_yyyymmdd,
    const std::string& available_version,
    std::uint32_t available_date_yyyymmdd) {
    const auto installed = parse_driver_version(installed_version);
    const auto available = parse_driver_version(available_version);
    if (!installed || !available) return false;
    if (installed->parts == available->parts) return false;

    // For equally ranked hardware matches Windows compares DriverVer date
    // before its four-part version. Realtek uses parallel version branches,
    // so a later release can have a numerically smaller third component.
    if (installed_date_yyyymmdd != 0 && available_date_yyyymmdd != 0 &&
        installed_date_yyyymmdd != available_date_yyyymmdd) {
        return available_date_yyyymmdd > installed_date_yyyymmdd;
    }
    return available->parts > installed->parts;
}

RecommendationResult recommend_driver_action(
    const DeviceRecord& device,
    const Candidate& candidate,
    const RecommendationContext& context) {
    std::vector<std::string> trace;
    if (context.user_ignored) {
        return result(RecommendationDecision::ignore, {"device or package is on the ignore list"});
    }
    if (!device.present) {
        return result(RecommendationDecision::ignore, {"device is not currently present"});
    }
    if (!context.catalog_resolution_unambiguous) {
        return result(RecommendationDecision::manual_review,
                      {"catalog resolution did not produce a unique package"});
    }
    if (!context.package_hash_verified) {
        return result(RecommendationDecision::ignore,
                      {"downloaded package hash does not match catalog metadata"});
    }
    if (!context.catalog_signature_valid || !candidate.signature_valid) {
        return result(RecommendationDecision::ignore,
                      {"package or catalog signature is not valid"});
    }
    if (!candidate.architecture_applicable || !candidate.os_applicable) {
        return result(RecommendationDecision::ignore,
                      {"candidate is not applicable to the running Windows target"});
    }
    if (candidate.blocked_by_windows) {
        return result(RecommendationDecision::ignore,
                      {"Windows marked the candidate as unusable"});
    }

    const bool exact_match = exact_hardware_match(device, candidate);
    trace.push_back(exact_match ? "candidate matches an exact hardware ID"
                                : "candidate matches only a compatible ID");
    const auto installed_version = parse_driver_version(device.installed_driver.version);
    if (!installed_version) {
        trace.push_back("installed driver version is unavailable or not comparable");
        return result(exact_match ? RecommendationDecision::update
                                  : RecommendationDecision::manual_review,
                      std::move(trace));
    }

    const bool same_provider = normalize(device.installed_driver.provider) ==
                               normalize(candidate.provider);
    if (!same_provider) {
        trace.push_back("installed and candidate providers differ");
        if (exact_match && installed_match_is_compatible(device)) {
            trace.push_back("candidate improves compatible-ID binding to an exact hardware-ID binding");
            if (candidate.driver_date >= device.installed_driver.date_yyyymmdd) {
                return result(RecommendationDecision::update, std::move(trace));
            }
            trace.push_back("candidate date is older than the installed driver date");
        }
        return result(RecommendationDecision::manual_review, std::move(trace));
    }

    if (candidate.driver_version.parts == installed_version->parts) {
        trace.push_back("candidate and installed versions are equal");
        return result(RecommendationDecision::keep, std::move(trace));
    }
    if (candidate.driver_date != 0 && device.installed_driver.date_yyyymmdd != 0 &&
        candidate.driver_date > device.installed_driver.date_yyyymmdd) {
        trace.push_back("candidate INF date is newer than the installed INF date");
        return result(RecommendationDecision::update, std::move(trace));
    }
    if (candidate.driver_version.parts < installed_version->parts) {
        trace.push_back("candidate version is lower than the installed version");
        return result(context.allow_downgrade ? RecommendationDecision::manual_review
                                              : RecommendationDecision::keep,
                      std::move(trace));
    }

    trace.push_back("candidate version is higher than the installed version");
    if (candidate.driver_date != 0 && device.installed_driver.date_yyyymmdd != 0 &&
        candidate.driver_date < device.installed_driver.date_yyyymmdd) {
        trace.push_back("candidate version is higher but its date is older");
        return result(RecommendationDecision::manual_review, std::move(trace));
    }
    return result(RecommendationDecision::update, std::move(trace));
}

std::string recommendation_decision_name(RecommendationDecision decision) {
    switch (decision) {
        case RecommendationDecision::update: return "update";
        case RecommendationDecision::keep: return "keep";
        case RecommendationDecision::ignore: return "ignore";
        case RecommendationDecision::manual_review: return "manual_review";
    }
    return "manual_review";
}

}  // namespace open_driver
