# Language: Python 3.11+; file: bootstrap_vcpkg.py; target: Windows 11.
from __future__ import annotations

import json
import pathlib
import shutil
import urllib.request
import zipfile


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[2]
    work = root / "work"
    archive = work / "vcpkg-master.zip"
    destination = work / "vcpkg-src"
    baseline_file = work / "vcpkg-baseline.txt"
    if archive.exists() or destination.exists() or baseline_file.exists():
        raise SystemExit("vcpkg bootstrap destination already exists")

    request = urllib.request.Request(
        "https://api.github.com/repos/microsoft/vcpkg/commits/master",
        headers={"Accept": "application/vnd.github+json", "User-Agent": "OpenDriverCore/1.0"},
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        baseline = json.load(response)["sha"]

    work.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(
        "https://codeload.github.com/microsoft/vcpkg/zip/refs/heads/master", archive
    )
    temporary = work / "vcpkg-extract"
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            target = (temporary / member.filename).resolve()
            if temporary.resolve() not in target.parents and target != temporary.resolve():
                raise SystemExit(f"unsafe zip entry: {member.filename}")
        package.extractall(temporary)
    extracted = temporary / "vcpkg-master"
    if not extracted.is_dir():
        raise SystemExit("vcpkg archive did not contain vcpkg-master")
    shutil.move(str(extracted), str(destination))
    metadata = (destination / "scripts" / "vcpkg-tool-metadata.txt").read_text(
        encoding="ascii"
    )
    release_tag = next(
        line.split("=", 1)[1]
        for line in metadata.splitlines()
        if line.startswith("VCPKG_TOOL_RELEASE_TAG=")
    )
    urllib.request.urlretrieve(
        f"https://github.com/microsoft/vcpkg-tool/releases/download/{release_tag}/vcpkg.exe",
        destination / "vcpkg.exe",
    )
    baseline_file.write_text(baseline + "\n", encoding="ascii")
    print(f"baseline={baseline}")
    print(f"vcpkg_root={destination}")


if __name__ == "__main__":
    main()
