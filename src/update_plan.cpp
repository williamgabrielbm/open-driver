// Language: C++20; file: update_plan.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "update_plan.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <variant>

namespace open_driver {
namespace {

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    std::variant<std::nullptr_t, bool, std::uint64_t, std::string, Array, Object> value;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    std::optional<JsonValue> parse(std::string& error) {
        skip_space();
        auto value = parse_value(error);
        if (!value) return std::nullopt;
        skip_space();
        if (position_ != input_.size()) {
            error = "unexpected trailing JSON content";
            return std::nullopt;
        }
        return value;
    }

private:
    const std::string& input_;
    std::size_t position_{};

    void skip_space() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skip_space();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    std::optional<JsonValue> parse_value(std::string& error) {
        skip_space();
        if (position_ >= input_.size()) {
            error = "unexpected end of JSON";
            return std::nullopt;
        }
        const char current = input_[position_];
        if (current == '{') return parse_object(error);
        if (current == '[') return parse_array(error);
        if (current == '"') {
            auto text = parse_string(error);
            if (!text) return std::nullopt;
            return JsonValue{.value = std::move(*text)};
        }
        if (current >= '0' && current <= '9') return parse_number(error);
        if (input_.compare(position_, 4, "true") == 0) {
            position_ += 4;
            return JsonValue{.value = true};
        }
        if (input_.compare(position_, 5, "false") == 0) {
            position_ += 5;
            return JsonValue{.value = false};
        }
        if (input_.compare(position_, 4, "null") == 0) {
            position_ += 4;
            return JsonValue{.value = nullptr};
        }
        error = "unsupported JSON value at byte " + std::to_string(position_);
        return std::nullopt;
    }

    std::optional<JsonValue> parse_object(std::string& error) {
        if (!consume('{')) return std::nullopt;
        JsonValue::Object object;
        skip_space();
        if (consume('}')) return JsonValue{.value = std::move(object)};
        while (true) {
            auto key = parse_string(error);
            if (!key) return std::nullopt;
            if (!consume(':')) {
                error = "expected ':' after object key";
                return std::nullopt;
            }
            auto value = parse_value(error);
            if (!value) return std::nullopt;
            if (!object.emplace(std::move(*key), std::move(*value)).second) {
                error = "duplicate JSON object key";
                return std::nullopt;
            }
            if (consume('}')) break;
            if (!consume(',')) {
                error = "expected ',' or '}' in object";
                return std::nullopt;
            }
        }
        return JsonValue{.value = std::move(object)};
    }

    std::optional<JsonValue> parse_array(std::string& error) {
        if (!consume('[')) return std::nullopt;
        JsonValue::Array array;
        skip_space();
        if (consume(']')) return JsonValue{.value = std::move(array)};
        while (true) {
            auto value = parse_value(error);
            if (!value) return std::nullopt;
            array.push_back(std::move(*value));
            if (consume(']')) break;
            if (!consume(',')) {
                error = "expected ',' or ']' in array";
                return std::nullopt;
            }
        }
        return JsonValue{.value = std::move(array)};
    }

    std::optional<std::string> parse_string(std::string& error) {
        skip_space();
        if (position_ >= input_.size() || input_[position_] != '"') {
            error = "expected JSON string";
            return std::nullopt;
        }
        ++position_;
        std::string result;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') return result;
            if (static_cast<unsigned char>(character) < 0x20) {
                error = "unescaped control character in JSON string";
                return std::nullopt;
            }
            if (character != '\\') {
                result.push_back(character);
                continue;
            }
            if (position_ >= input_.size()) {
                error = "unterminated JSON escape";
                return std::nullopt;
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    if (position_ + 4 > input_.size()) {
                        error = "truncated JSON Unicode escape";
                        return std::nullopt;
                    }
                    unsigned int code = 0;
                    for (int index = 0; index < 4; ++index) {
                        const char digit = input_[position_++];
                        code <<= 4;
                        if (digit >= '0' && digit <= '9') code += digit - '0';
                        else if (digit >= 'a' && digit <= 'f') code += digit - 'a' + 10;
                        else if (digit >= 'A' && digit <= 'F') code += digit - 'A' + 10;
                        else {
                            error = "invalid JSON Unicode escape";
                            return std::nullopt;
                        }
                    }
                    if (code <= 0x7f) result.push_back(static_cast<char>(code));
                    else if (code <= 0x7ff) {
                        result.push_back(static_cast<char>(0xc0 | (code >> 6)));
                        result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    } else {
                        result.push_back(static_cast<char>(0xe0 | (code >> 12)));
                        result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                        result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    }
                    break;
                }
                default:
                    error = "invalid JSON escape";
                    return std::nullopt;
            }
        }
        error = "unterminated JSON string";
        return std::nullopt;
    }

    std::optional<JsonValue> parse_number(std::string& error) {
        const std::size_t start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
        }
        std::uint64_t number = 0;
        const auto parsed = std::from_chars(
            input_.data() + start, input_.data() + position_, number);
        if (parsed.ec != std::errc{}) {
            error = "invalid JSON integer";
            return std::nullopt;
        }
        return JsonValue{.value = number};
    }
};

const JsonValue* member(const JsonValue& value, const std::string& key) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (!object) return nullptr;
    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

bool read_string(const JsonValue& value, const std::string& key, std::string& output) {
    const auto* item = member(value, key);
    if (!item) return false;
    const auto* text = std::get_if<std::string>(&item->value);
    if (!text) return false;
    output = *text;
    return true;
}

bool read_bool(const JsonValue& value, const std::string& key, bool& output) {
    const auto* item = member(value, key);
    if (!item) return false;
    const auto* boolean = std::get_if<bool>(&item->value);
    if (!boolean) return false;
    output = *boolean;
    return true;
}

bool read_u32(const JsonValue& value, const std::string& key, std::uint32_t& output) {
    const auto* item = member(value, key);
    if (!item) return false;
    const auto* number = std::get_if<std::uint64_t>(&item->value);
    if (!number || *number > UINT32_MAX) return false;
    output = static_cast<std::uint32_t>(*number);
    return true;
}

bool read_strings(const JsonValue& value, const std::string& key, std::vector<std::string>& output) {
    const auto* item = member(value, key);
    if (!item) return false;
    const auto* array = std::get_if<JsonValue::Array>(&item->value);
    if (!array) return false;
    output.clear();
    for (const auto& entry : *array) {
        const auto* text = std::get_if<std::string>(&entry.value);
        if (!text) return false;
        output.push_back(*text);
    }
    return true;
}

bool read_commands(
    const JsonValue& value,
    const std::string& key,
    std::vector<PlannedCommand>& output) {
    const auto* item = member(value, key);
    if (!item) return false;
    const auto* array = std::get_if<JsonValue::Array>(&item->value);
    if (!array) return false;
    output.clear();
    for (const auto& entry : *array) {
        PlannedCommand command_value;
        if (!read_string(entry, "id", command_value.id) ||
            !read_string(entry, "phase", command_value.phase) ||
            !read_string(entry, "executable", command_value.executable) ||
            !read_strings(entry, "arguments", command_value.arguments) ||
            !read_bool(entry, "mutates_system", command_value.mutates_system) ||
            !read_bool(entry, "requires_elevation", command_value.requires_elevation)) {
            return false;
        }
        output.push_back(std::move(command_value));
    }
    return true;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character) << std::dec;
                } else {
                    output << character;
                }
        }
    }
    return output.str();
}

void string_value(std::ostream& output, const std::string& value) {
    output << '"' << json_escape(value) << '"';
}

void string_array(std::ostream& output, const std::vector<std::string>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        string_value(output, values[index]);
    }
    output << ']';
}

std::string version_string(const DriverVersion& version) {
    std::ostringstream output;
    output << version.parts[0] << '.' << version.parts[1] << '.'
           << version.parts[2] << '.' << version.parts[3];
    return output.str();
}

bool exact_hardware_match(const DeviceRecord& device, const Candidate& candidate) {
    if (candidate.match_kind != MatchKind::hardware_id) return false;
    const auto matching = lower_ascii(candidate.matching_id);
    return std::any_of(device.hardware_ids.begin(), device.hardware_ids.end(), [&](const auto& id) {
        return lower_ascii(id) == matching;
    });
}

bool unsafe_command_value(const std::string& value) {
    return value.empty() || value.find_first_of("\r\n\0") != std::string::npos;
}

std::string quote_command_argument(const std::string& value) {
    if (value.find_first_of(" \t\"") == std::string::npos) return value;
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char character : value) {
        if (character == '\\') {
            ++backslashes;
        } else if (character == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, '\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

std::uint64_t fnv1a_64(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string make_plan_id(
    const DeviceRecord& device,
    const Candidate& candidate,
    const PackageEvidence& package) {
    const std::string canonical = lower_ascii(device.instance_id) + "|" +
        lower_ascii(candidate.matching_id) + "|" + lower_ascii(package.catalog_update_id) + "|" +
        package.cab_sha256_base64 + "|" + lower_ascii(package.inf_sha256_hex);
    std::ostringstream output;
    output << "odc-" << std::hex << std::setfill('0') << std::setw(16) << fnv1a_64(canonical);
    return output.str();
}

PlannedCommand command(
    std::string id,
    std::string phase,
    std::string executable,
    std::vector<std::string> arguments,
    bool mutates) {
    return {
        .id = std::move(id),
        .phase = std::move(phase),
        .executable = std::move(executable),
        .arguments = std::move(arguments),
        .mutates_system = mutates,
        .requires_elevation = mutates,
    };
}

void write_command(std::ostream& output, const PlannedCommand& value) {
    output << "{\"id\":"; string_value(output, value.id);
    output << ",\"phase\":"; string_value(output, value.phase);
    output << ",\"executable\":"; string_value(output, value.executable);
    output << ",\"arguments\":"; string_array(output, value.arguments);
    output << ",\"preview\":"; string_value(output, planned_command_preview(value));
    output << ",\"mutates_system\":" << (value.mutates_system ? "true" : "false")
           << ",\"requires_elevation\":" << (value.requires_elevation ? "true" : "false")
           << '}';
}

void write_commands(std::ostream& output, const std::vector<PlannedCommand>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        write_command(output, values[index]);
    }
    output << ']';
}

}  // namespace

std::string planned_command_preview(const PlannedCommand& command) {
    std::string preview = quote_command_argument(command.executable);
    for (const auto& argument : command.arguments) {
        preview.push_back(' ');
        preview += quote_command_argument(argument);
    }
    return preview;
}

UpdatePlan build_update_plan(
    const DeviceRecord& device,
    const Candidate& candidate,
    const RecommendationResult& recommendation,
    const PackageEvidence& package,
    const UpdatePlanOptions& options) {
    UpdatePlan plan;
    plan.device_instance_id = device.instance_id;
    plan.selected_hardware_id = candidate.matching_id;
    plan.installed_driver = device.installed_driver;
    plan.candidate = candidate;
    plan.package = package;
    plan.plan_id = make_plan_id(device, candidate, package);

    if (recommendation.decision != RecommendationDecision::update) {
        plan.blockers.push_back("recommendation is not update");
    }
    if (!device.present) plan.blockers.push_back("device is not currently present");
    if (!exact_hardware_match(device, candidate)) {
        plan.blockers.push_back("candidate does not match an exact hardware ID");
    }
    if (!package.cab_hash_verified || package.cab_sha256_base64.empty()) {
        plan.blockers.push_back("CAB SHA-256 is missing or unverified");
    }
    if (!package.catalog_signature_valid || package.catalog_path.empty()) {
        plan.blockers.push_back("catalog signature is missing or invalid");
    }
    if (!candidate.signature_valid || !candidate.architecture_applicable ||
        !candidate.os_applicable || candidate.blocked_by_windows) {
        plan.blockers.push_back("candidate trust or applicability checks failed");
    }
    if (candidate.inf_path.empty() || package.inf_path.empty() ||
        lower_ascii(candidate.inf_path) != lower_ascii(package.inf_path)) {
        plan.blockers.push_back("candidate INF and package evidence do not identify the same path");
    }
    if (candidate.package_hash.empty() || package.inf_sha256_hex.empty() ||
        lower_ascii(candidate.package_hash) != lower_ascii(package.inf_sha256_hex)) {
        plan.blockers.push_back("candidate INF SHA-256 does not match package evidence");
    }
    if (package.catalog_update_id.empty()) {
        plan.blockers.push_back("catalog update ID is missing");
    }
    if (options.rollback_directory.empty()) {
        plan.blockers.push_back("rollback directory is missing");
    }
    if (device.installed_driver.inf_path.empty()) {
        plan.blockers.push_back("installed INF is unavailable for rollback export");
    }
    if (unsafe_command_value(device.instance_id) || unsafe_command_value(candidate.inf_path) ||
        unsafe_command_value(options.rollback_directory)) {
        plan.blockers.push_back("a command input is empty or contains a control character");
    }

    plan.preconditions = {
        "re-enumerate the device and require present=true",
        "recompute the CAB SHA-256 and compare it with catalog metadata",
        "recompute the INF SHA-256 and compare it with this plan",
        "revalidate the catalog and candidate signatures",
        "re-run candidate ranking and require the same Hardware ID and rank",
        "re-run RecommendationEngine and require decision=update",
        "require a new or empty rollback directory",
    };

    if (!plan.blockers.empty()) return plan;

    const auto installed_inf_name = std::filesystem::path(device.installed_driver.inf_path)
                                        .filename().string();
    plan.steps.push_back(command(
        "inspect-current-driver", "preflight", "pnputil.exe",
        {"/enum-devices", "/instanceid", device.instance_id, "/drivers"}, false));
    plan.steps.push_back(command(
        "export-current-driver", "preflight", "pnputil.exe",
        {"/export-driver", installed_inf_name, options.rollback_directory}, true));
    if (options.create_restore_point) {
        plan.steps.push_back(command(
            "create-restore-point", "preflight", "powershell.exe",
            {"-NoProfile", "-Command",
             "Checkpoint-Computer -Description '" + plan.plan_id +
                 "' -RestorePointType 'MODIFY_SETTINGS'"},
            true));
    }
    plan.steps.push_back(command(
        "install-selected-package", "apply", "pnputil.exe",
        {"/add-driver", candidate.inf_path, "/install"}, true));
    plan.steps.push_back(command(
        "verify-selected-driver", "verify", "pnputil.exe",
        {"/enum-devices", "/instanceid", device.instance_id, "/drivers"}, false));

    const auto rollback_inf = (std::filesystem::path(options.rollback_directory) /
                               installed_inf_name).string();
    plan.rollback_steps.push_back(command(
        "restore-exported-driver", "rollback", "pnputil.exe",
        {"/add-driver", rollback_inf, "/install"}, true));
    plan.rollback_steps.push_back(command(
        "verify-restored-driver", "rollback-verify", "pnputil.exe",
        {"/enum-devices", "/instanceid", device.instance_id, "/drivers"}, false));
    plan.ready = true;
    return plan;
}

std::string update_plan_json(const UpdatePlan& plan) {
    std::ostringstream output;
    output << "{\n  \"schema_version\":" << plan.schema_version << ",\n  \"plan_id\":";
    string_value(output, plan.plan_id);
    output << ",\n  \"ready\":" << (plan.ready ? "true" : "false")
           << ",\n  \"blockers\":";
    string_array(output, plan.blockers);
    output << ",\n  \"preconditions\":";
    string_array(output, plan.preconditions);
    output << ",\n  \"device\":{\"instance_id\":";
    string_value(output, plan.device_instance_id);
    output << ",\"selected_hardware_id\":";
    string_value(output, plan.selected_hardware_id);
    output << ",\"installed_driver\":{\"provider\":";
    string_value(output, plan.installed_driver.provider);
    output << ",\"version\":"; string_value(output, plan.installed_driver.version);
    output << ",\"date\":" << plan.installed_driver.date_yyyymmdd << ",\"inf_path\":";
    string_value(output, plan.installed_driver.inf_path);
    output << "}},\n  \"candidate\":{\"provider\":";
    string_value(output, plan.candidate.provider);
    output << ",\"version\":"; string_value(output, version_string(plan.candidate.driver_version));
    output << ",\"date\":" << plan.candidate.driver_date << ",\"inf_path\":";
    string_value(output, plan.candidate.inf_path);
    output << ",\"inf_sha256_hex\":"; string_value(output, plan.candidate.package_hash);
    output << ",\"windows_rank\":" << plan.candidate.windows_rank << "},\n  \"package\":{\"catalog_update_id\":";
    string_value(output, plan.package.catalog_update_id);
    output << ",\"cab_path\":"; string_value(output, plan.package.cab_path);
    output << ",\"cab_sha256_base64\":"; string_value(output, plan.package.cab_sha256_base64);
    output << ",\"cab_hash_verified\":" << (plan.package.cab_hash_verified ? "true" : "false")
           << ",\"catalog_path\":"; string_value(output, plan.package.catalog_path);
    output << ",\"catalog_signature_valid\":"
           << (plan.package.catalog_signature_valid ? "true" : "false") << "},\n  \"steps\":";
    write_commands(output, plan.steps);
    output << ",\n  \"rollback_steps\":";
    write_commands(output, plan.rollback_steps);
    output << "\n}\n";
    return output.str();
}

UpdatePlanParseResult parse_update_plan_json(const std::string& json) {
    std::string error;
    JsonParser parser(json);
    const auto root = parser.parse(error);
    if (!root) return {.error = std::move(error)};

    UpdatePlan plan;
    if (!read_u32(*root, "schema_version", plan.schema_version) || plan.schema_version != 1 ||
        !read_string(*root, "plan_id", plan.plan_id) ||
        !read_bool(*root, "ready", plan.ready) ||
        !read_strings(*root, "blockers", plan.blockers) ||
        !read_strings(*root, "preconditions", plan.preconditions)) {
        return {.error = "invalid or unsupported update plan header"};
    }

    const auto* device = member(*root, "device");
    const auto* installed = device ? member(*device, "installed_driver") : nullptr;
    if (!device || !installed ||
        !read_string(*device, "instance_id", plan.device_instance_id) ||
        !read_string(*device, "selected_hardware_id", plan.selected_hardware_id) ||
        !read_string(*installed, "provider", plan.installed_driver.provider) ||
        !read_string(*installed, "version", plan.installed_driver.version) ||
        !read_u32(*installed, "date", plan.installed_driver.date_yyyymmdd) ||
        !read_string(*installed, "inf_path", plan.installed_driver.inf_path)) {
        return {.error = "invalid device snapshot in update plan"};
    }

    const auto* candidate = member(*root, "candidate");
    std::string candidate_version;
    if (!candidate ||
        !read_string(*candidate, "provider", plan.candidate.provider) ||
        !read_string(*candidate, "version", candidate_version) ||
        !read_u32(*candidate, "date", plan.candidate.driver_date) ||
        !read_string(*candidate, "inf_path", plan.candidate.inf_path) ||
        !read_string(*candidate, "inf_sha256_hex", plan.candidate.package_hash) ||
        !read_u32(*candidate, "windows_rank", plan.candidate.windows_rank)) {
        return {.error = "invalid candidate snapshot in update plan"};
    }
    const auto parsed_version = parse_driver_version(candidate_version);
    if (!parsed_version) return {.error = "invalid candidate version in update plan"};
    plan.candidate.driver_version = *parsed_version;
    plan.candidate.matching_id = plan.selected_hardware_id;
    plan.candidate.match_kind = MatchKind::hardware_id;

    const auto* package = member(*root, "package");
    if (!package ||
        !read_string(*package, "catalog_update_id", plan.package.catalog_update_id) ||
        !read_string(*package, "cab_path", plan.package.cab_path) ||
        !read_string(*package, "cab_sha256_base64", plan.package.cab_sha256_base64) ||
        !read_bool(*package, "cab_hash_verified", plan.package.cab_hash_verified) ||
        !read_string(*package, "catalog_path", plan.package.catalog_path) ||
        !read_bool(*package, "catalog_signature_valid", plan.package.catalog_signature_valid)) {
        return {.error = "invalid package evidence in update plan"};
    }
    plan.package.inf_path = plan.candidate.inf_path;
    plan.package.inf_sha256_hex = plan.candidate.package_hash;

    if (!read_commands(*root, "steps", plan.steps) ||
        !read_commands(*root, "rollback_steps", plan.rollback_steps)) {
        return {.error = "invalid command list in update plan"};
    }
    if (plan.plan_id.empty() || plan.device_instance_id.empty()) {
        return {.error = "update plan identity is empty"};
    }
    return {.plan = std::move(plan)};
}

}  // namespace open_driver
