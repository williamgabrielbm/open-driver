// Language: C++20; file: external_package_source.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "local_driver_store.hpp"

#include <string>

namespace open_driver {

[[nodiscard]] LocalCandidateResult analyze_external_inf_for_device(
    const DeviceRecord& device,
    const std::string& inf_path);

}  // namespace open_driver
