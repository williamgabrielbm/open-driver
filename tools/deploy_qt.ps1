param(
    [string]$BuildDirectory = "build-qt",
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "dist/OpenDriver"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$qtRoot = Join-Path $projectRoot "third_party/Qt/6.8.3/msvc2022_64"
$binary = Join-Path $projectRoot "$BuildDirectory/$Configuration/open_driver_gui.exe"
$transaction = Join-Path $projectRoot "$BuildDirectory/$Configuration/open_driver_transaction.exe"
$destination = Join-Path $projectRoot $OutputDirectory

if (-not (Test-Path -LiteralPath $binary)) { throw "GUI não compilada: $binary" }
if (-not (Test-Path -LiteralPath $transaction)) { throw "Executor não compilado: $transaction" }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -LiteralPath $binary -Destination (Join-Path $destination "open_driver_gui.exe") -Force
Copy-Item -LiteralPath $transaction -Destination (Join-Path $destination "open_driver_transaction.exe") -Force

$visualStudioRoot = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio"
$vcInstall = Get-ChildItem -Path $visualStudioRoot -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue } |
    ForEach-Object { Join-Path $_.FullName "VC" } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if ($vcInstall) {
    $env:VCINSTALLDIR = $vcInstall
    $redist = Get-ChildItem -Path (Join-Path $vcInstall "Redist/MSVC") -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d' } | Sort-Object Name -Descending | Select-Object -First 1
    if ($redist) {
        $env:VCToolsRedistDir = $redist.FullName + "\"
        $crtDirectory = Get-ChildItem -Path (Join-Path $redist.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($crtDirectory) {
            Copy-Item -Path (Join-Path $crtDirectory.FullName "*.dll") -Destination $destination -Force
        }
    }
}

& (Join-Path $qtRoot "bin/windeployqt.exe") `
    --release `
    --compiler-runtime `
    --no-system-dxc-compiler `
    --qmldir (Join-Path $projectRoot "qml") `
    --no-translations `
    (Join-Path $destination "open_driver_gui.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt falhou com código $LASTEXITCODE" }

$licenseTarget = Join-Path $destination "licenses"
New-Item -ItemType Directory -Path $licenseTarget -Force | Out-Null
$licenseSource = Join-Path $qtRoot "LICENSES"
if (Test-Path -LiteralPath $licenseSource) {
    Copy-Item -LiteralPath $licenseSource -Destination $licenseTarget -Recurse -Force
}
$notices = Join-Path $projectRoot "THIRD_PARTY_NOTICES.md"
if (Test-Path -LiteralPath $notices) {
    Copy-Item -LiteralPath $notices -Destination $destination -Force
}
$vcpkgRoot = Join-Path (Split-Path $projectRoot -Parent) "work/vcpkg-installed/x64-windows-static-md/share"
foreach ($package in @("curl", "openssl", "zlib")) {
    $copyright = Join-Path $vcpkgRoot "$package/copyright"
    if (Test-Path -LiteralPath $copyright) {
        Copy-Item -LiteralPath $copyright -Destination (Join-Path $licenseTarget "$package-copyright.txt") -Force
    }
}
Write-Host "Pacote pronto em $destination"
