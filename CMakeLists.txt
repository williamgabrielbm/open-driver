cmake_minimum_required(VERSION 3.24)
project(open_driver_core LANGUAGES CXX)

option(OPEN_DRIVER_ENABLE_CURL_OPENSSL
    "Enable the libcurl transport and require its OpenSSL TLS backend" OFF)

if(MSVC AND OPEN_DRIVER_ENABLE_CURL_OPENSSL)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_library(open_driver_core
    src/catalog_resolver.cpp
    src/candidate_ranker.cpp
    src/device_inventory.cpp
    src/driver_health.cpp
    src/local_driver_store.cpp
    src/microsoft_catalog.cpp
    src/official_driver_source.cpp
    src/package_materializer.cpp
    src/pipeline.cpp
    src/recommendation_engine.cpp
    src/transaction_executor.cpp
    src/update_orchestrator.cpp
    src/update_plan.cpp
    src/windows_update_source.cpp
)

target_include_directories(open_driver_core PUBLIC src)
target_compile_features(open_driver_core PUBLIC cxx_std_20)

if(WIN32)
    target_compile_definitions(open_driver_core PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(open_driver_core PUBLIC setupapi cfgmgr32 bcrypt crypt32 winhttp wininet wintrust advapi32 ole32 oleaut32 wuguid)
endif()

if(OPEN_DRIVER_ENABLE_CURL_OPENSSL)
    find_package(CURL REQUIRED)
    target_compile_definitions(open_driver_core PRIVATE OPEN_DRIVER_HAS_CURL_OPENSSL)
    target_link_libraries(open_driver_core PUBLIC CURL::libcurl)
endif()

if(MSVC)
    add_compile_options(/utf-8)
    target_compile_options(open_driver_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(open_driver_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(candidate_ranker_tests
    tests/candidate_ranker_tests.cpp
)

target_link_libraries(candidate_ranker_tests PRIVATE open_driver_core)
add_test(NAME candidate_ranker_tests COMMAND candidate_ranker_tests)

add_executable(device_inventory_tests
    tests/device_inventory_tests.cpp
)

target_link_libraries(device_inventory_tests PRIVATE open_driver_core)
add_test(NAME device_inventory_tests COMMAND device_inventory_tests)

add_executable(driver_health_tests
    tests/driver_health_tests.cpp
)

target_link_libraries(driver_health_tests PRIVATE open_driver_core)
add_test(NAME driver_health_tests COMMAND driver_health_tests)

add_executable(open_driver_scan
    src/scanner_cli.cpp
)

target_link_libraries(open_driver_scan PRIVATE open_driver_core)

add_executable(local_candidate_tests
    tests/local_candidate_tests.cpp
)

target_link_libraries(local_candidate_tests PRIVATE open_driver_core)
add_test(NAME local_candidate_tests COMMAND local_candidate_tests)

add_executable(open_driver_candidates
    src/candidate_scan_cli.cpp
)

target_link_libraries(open_driver_candidates PRIVATE open_driver_core)

add_executable(external_package_tests
    tests/external_package_tests.cpp
)

target_link_libraries(external_package_tests PRIVATE open_driver_core)
add_test(NAME external_package_tests COMMAND external_package_tests)

add_executable(open_driver_external_inf
    src/external_inf_cli.cpp
)

target_link_libraries(open_driver_external_inf PRIVATE open_driver_core)

add_executable(open_driver_windows_update
    src/windows_update_cli.cpp
)

target_link_libraries(open_driver_windows_update PRIVATE open_driver_core)

add_executable(package_materializer_tests
    tests/package_materializer_tests.cpp
)

target_link_libraries(package_materializer_tests PRIVATE open_driver_core)
add_test(NAME package_materializer_tests COMMAND package_materializer_tests)

add_executable(open_driver_package
    src/package_materializer_cli.cpp
)

target_link_libraries(open_driver_package PRIVATE open_driver_core)

add_executable(catalog_resolver_tests
    tests/catalog_resolver_tests.cpp
)

target_link_libraries(catalog_resolver_tests PRIVATE open_driver_core)
add_test(NAME catalog_resolver_tests COMMAND catalog_resolver_tests)

add_executable(open_driver_catalog_resolver
    src/catalog_resolver_cli.cpp
)

target_link_libraries(open_driver_catalog_resolver PRIVATE open_driver_core)

add_executable(recommendation_engine_tests
    tests/recommendation_engine_tests.cpp
)

target_link_libraries(recommendation_engine_tests PRIVATE open_driver_core)
add_test(NAME recommendation_engine_tests COMMAND recommendation_engine_tests)

add_executable(open_driver_recommendation
    src/recommendation_cli.cpp
)

target_link_libraries(open_driver_recommendation PRIVATE open_driver_core)

add_executable(update_plan_tests
    tests/update_plan_tests.cpp
)

target_link_libraries(update_plan_tests PRIVATE open_driver_core)
add_test(NAME update_plan_tests COMMAND update_plan_tests)

add_executable(open_driver_update_plan
    src/update_plan_cli.cpp
)

target_link_libraries(open_driver_update_plan PRIVATE open_driver_core)

add_executable(transaction_executor_tests
    tests/transaction_executor_tests.cpp
)

target_link_libraries(transaction_executor_tests PRIVATE open_driver_core)
add_test(NAME transaction_executor_tests COMMAND transaction_executor_tests)

add_executable(open_driver_transaction
    src/transaction_executor_cli.cpp
)

target_link_libraries(open_driver_transaction PRIVATE open_driver_core)

add_executable(pipeline_tests
    tests/pipeline_tests.cpp
)

target_link_libraries(pipeline_tests PRIVATE open_driver_core)
add_test(NAME pipeline_tests COMMAND pipeline_tests)

add_executable(update_orchestrator_tests
    tests/update_orchestrator_tests.cpp
)

target_link_libraries(update_orchestrator_tests PRIVATE open_driver_core)
add_test(NAME update_orchestrator_tests COMMAND update_orchestrator_tests)

add_executable(open_driver_discover_updates
    src/update_discovery_cli.cpp
)

target_link_libraries(open_driver_discover_updates PRIVATE open_driver_core)

add_executable(open_driver_pipeline
    src/pipeline_cli.cpp
)

target_link_libraries(open_driver_pipeline PRIVATE open_driver_core)

if(WIN32)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.8)
    add_executable(open_driver_gui WIN32
        src/qt_main.cpp
        src/app_controller.cpp
        src/app_controller.hpp
        src/driver_list_model.cpp
        src/driver_list_model.hpp
    )
    qt_add_qml_module(open_driver_gui
        URI OpenDriver
        VERSION 1.0
        QML_FILES
            qml/Main.qml
            qml/Theme.js
            qml/components/Panel.qml
            qml/components/PremiumButton.qml
            qml/components/Sidebar.qml
            qml/components/ScanRing.qml
            qml/components/SystemInfoPanel.qml
            qml/components/DriverRow.qml
            qml/pages/ScanPage.qml
            qml/pages/OptimizePage.qml
            qml/pages/ToolsPage.qml
            qml/pages/HistoryPage.qml
            qml/pages/SettingsPage.qml
    )
    target_compile_definitions(open_driver_gui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_link_options(open_driver_gui PRIVATE
            "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    endif()
    target_link_libraries(open_driver_gui PRIVATE
        open_driver_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
        shell32 dwmapi)
    add_dependencies(open_driver_gui open_driver_transaction)
    install(TARGETS open_driver_gui open_driver_transaction
        RUNTIME DESTINATION .)
endif()
