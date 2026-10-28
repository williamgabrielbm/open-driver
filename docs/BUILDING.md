# Compilação e distribuição

O aplicativo usa Qt 6.8.3 em bibliotecas dinâmicas. Os módulos utilizados (`Core`, `Gui`, `Qml`, `Quick` e `QuickControls2`) estão disponíveis sob LGPLv3/GPLv3 na edição comunitária. O pacote de distribuição mantém as DLLs separadas e inclui os textos de licença do Qt.

## Compilar

```powershell
$qt = (Resolve-Path .\third_party\Qt\6.8.3\msvc2022_64).Path
$workspace = (Resolve-Path ..).Path
$triplet = "$workspace\work\vcpkg-installed\x64-windows-static-md"
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S . -B build-qt-md -A x64 `
  -DCMAKE_PREFIX_PATH="$qt;$triplet" `
  -DOPEN_DRIVER_ENABLE_CURL_OPENSSL=ON `
  -DCMAKE_TOOLCHAIN_FILE="$workspace\work\vcpkg-src\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_MANIFEST_INSTALL=OFF `
  -DVCPKG_INSTALLED_DIR="$workspace\work\vcpkg-installed" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
& $cmake --build build-qt-md --config Release --parallel
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir build-qt-md -C Release --output-on-failure
```

## Gerar a pasta pronta para execução

```powershell
.\tools\deploy_qt.ps1 -BuildDirectory build-qt-md -Configuration Release
```

O `windeployqt` copia as bibliotecas, plugins gráficos e módulos QML necessários para `dist\OpenDriver`. O executor transacional fica ao lado da interface. Em outra máquina, instale o Microsoft Visual C++ Redistributable oficial correspondente ao MSVC 2022.

## Separação de responsabilidades

- `open_driver_core`: inventário, descoberta, validação, planos, assinatura e rollback.
- `AppController`: estado da aplicação, tarefas em segundo plano, confirmação e cancelamento.
- `DriverListModel`: dados reais aprovados pela validação final.
- `qml/`: apresentação, navegação, animações e acessibilidade visual.

Nenhuma instalação real é necessária para validar a interface. O executor permanece protegido por confirmação explícita, revalidação do dispositivo e plano transacional.
