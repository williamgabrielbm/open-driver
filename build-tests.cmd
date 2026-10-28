@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Installer vswhere.exe was not found.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
    echo Visual C++ Build Tools were not found.
    exit /b 1
)

call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp tests\candidate_ranker_tests.cpp /Fe:build\candidate_ranker_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\candidate_ranker_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\device_inventory.cpp tests\device_inventory_tests.cpp setupapi.lib cfgmgr32.lib ole32.lib /Fe:build\device_inventory_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\device_inventory_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\device_inventory.cpp src\scanner_cli.cpp setupapi.lib cfgmgr32.lib ole32.lib /Fe:build\open_driver_scan.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp tests\local_candidate_tests.cpp setupapi.lib cfgmgr32.lib bcrypt.lib ole32.lib /Fe:build\local_candidate_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\local_candidate_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp src\candidate_scan_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib ole32.lib /Fe:build\open_driver_candidates.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp tests\external_package_tests.cpp setupapi.lib cfgmgr32.lib bcrypt.lib ole32.lib /Fe:build\external_package_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\external_package_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp src\external_inf_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib ole32.lib /Fe:build\open_driver_external_inf.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\device_inventory.cpp src\windows_update_source.cpp src\windows_update_cli.cpp setupapi.lib cfgmgr32.lib ole32.lib oleaut32.lib wuguid.lib /Fe:build\open_driver_windows_update.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\microsoft_catalog.cpp src\package_materializer.cpp tests\package_materializer_tests.cpp setupapi.lib bcrypt.lib crypt32.lib winhttp.lib wininet.lib wintrust.lib /Fe:build\package_materializer_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\package_materializer_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp src\microsoft_catalog.cpp src\package_materializer.cpp src\package_materializer_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib crypt32.lib winhttp.lib wininet.lib wintrust.lib ole32.lib /Fe:build\open_driver_package.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\catalog_resolver.cpp tests\catalog_resolver_tests.cpp /Fe:build\catalog_resolver_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\catalog_resolver_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\catalog_resolver.cpp src\device_inventory.cpp src\microsoft_catalog.cpp src\windows_update_source.cpp src\catalog_resolver_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib crypt32.lib winhttp.lib wininet.lib ole32.lib oleaut32.lib wuguid.lib /Fe:build\open_driver_catalog_resolver.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\recommendation_engine.cpp tests\recommendation_engine_tests.cpp /Fe:build\recommendation_engine_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\recommendation_engine_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp src\recommendation_engine.cpp src\recommendation_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib ole32.lib /Fe:build\open_driver_recommendation.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\recommendation_engine.cpp src\update_plan.cpp tests\update_plan_tests.cpp /Fe:build\update_plan_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\update_plan_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp src\microsoft_catalog.cpp src\package_materializer.cpp src\recommendation_engine.cpp src\update_plan.cpp src\update_plan_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib crypt32.lib winhttp.lib wininet.lib wintrust.lib ole32.lib /Fe:build\open_driver_update_plan.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\recommendation_engine.cpp src\update_plan.cpp src\transaction_executor.cpp tests\transaction_executor_tests.cpp /Fe:build\transaction_executor_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\transaction_executor_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\candidate_ranker.cpp src\device_inventory.cpp src\local_driver_store.cpp src\microsoft_catalog.cpp src\package_materializer.cpp src\recommendation_engine.cpp src\update_plan.cpp src\transaction_executor.cpp src\transaction_executor_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib crypt32.lib winhttp.lib wininet.lib wintrust.lib ole32.lib /Fe:build\open_driver_transaction.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\pipeline.cpp tests\pipeline_tests.cpp /Fe:build\pipeline_tests.exe
if errorlevel 1 exit /b %errorlevel%

build\pipeline_tests.exe
if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src src\catalog_resolver.cpp src\device_inventory.cpp src\microsoft_catalog.cpp src\pipeline.cpp src\windows_update_source.cpp src\pipeline_cli.cpp setupapi.lib cfgmgr32.lib bcrypt.lib crypt32.lib winhttp.lib wininet.lib ole32.lib oleaut32.lib wuguid.lib /Fe:build\open_driver_pipeline.exe
exit /b %errorlevel%
