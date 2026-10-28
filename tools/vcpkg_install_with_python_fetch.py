# Language: Python 3.11+; file: vcpkg_install_with_python_fetch.py; target: Windows 11.
from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys
import urllib.request


DOWNLOAD = re.compile(r"Downloading (https://\S+) -> ([^\r\n]+)")
MIRRORED_DOWNLOAD = re.compile(r"Downloading ([^,\r\n]+), trying (https://\S+)")


def main() -> None:
    project = pathlib.Path(__file__).resolve().parents[1]
    root = project.parent
    work = root / "work"
    downloads = work / "vcpkg-downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    (work / "vcpkg-cache").mkdir(parents=True, exist_ok=True)
    (work / "vcpkg-registries").mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["VCPKG_DEFAULT_BINARY_CACHE"] = str(work / "vcpkg-cache")
    environment["X_VCPKG_REGISTRIES_CACHE"] = str(work / "vcpkg-registries")
    triplet = sys.argv[1] if len(sys.argv) > 1 else "x64-windows-static"
    package = sys.argv[2] if len(sys.argv) > 2 else ""
    command = [str(work / "vcpkg-src" / "vcpkg.exe"), "install"]
    if package:
        command.extend([package, "--classic"])
    else:
        command.append(f"--x-manifest-root={project}")
    command.extend([
        f"--triplet={triplet}",
        f"--x-install-root={work / 'vcpkg-installed'}",
        f"--x-buildtrees-root={work / 'vcpkg-buildtrees'}",
        f"--x-packages-root={work / 'vcpkg-packages'}",
        f"--downloads-root={downloads}",
        "--clean-after-build",
    ])

    for attempt in range(32):
        result = subprocess.run(
            command, cwd=root, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        print(result.stdout, end="")
        if result.returncode == 0:
            return
        matches = DOWNLOAD.findall(result.stdout)
        matches.extend(
            (url, file_name) for file_name, url in MIRRORED_DOWNLOAD.findall(result.stdout)
        )
        if not matches:
            raise SystemExit(result.returncode)
        fetched = False
        for url, file_name in matches:
            destination = downloads / file_name.strip()
            temporary = destination.with_name(destination.name + ".python-download")
            print(f"python_fetch={url}", flush=True)
            urllib.request.urlretrieve(url, temporary)
            temporary.replace(destination)
            fetched = True
        if not fetched:
            raise SystemExit(result.returncode)
    raise SystemExit("too many vcpkg download recovery attempts")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"bootstrap_error={error}", file=sys.stderr)
        raise
