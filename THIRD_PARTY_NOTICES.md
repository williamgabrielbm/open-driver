# Avisos de terceiros

O Open Driver distribui o Qt 6.8.3 como bibliotecas dinâmicas. Os módulos comunitários usados pela interface são disponibilizados pelo Qt Project sob LGPLv3/GPLv3. As DLLs permanecem separadas do executável e os textos integrais das licenças acompanham o pacote na pasta `licenses`.

- Qt 6.8.3: https://www.qt.io/ e https://doc.qt.io/qt-6/licensing.html
- libcurl 8.23.0: licença curl
- OpenSSL 3.6.5: Apache License 2.0
- zlib 1.3.2: licença zlib

Os textos de copyright de libcurl, OpenSSL e zlib são copiados para `licenses` pelo script de distribuição. O código-fonte correspondente a essas versões pode ser obtido nos projetos oficiais ou recompilado pelas receitas registradas no `vcpkg.json` e no histórico do vcpkg usado neste projeto.
