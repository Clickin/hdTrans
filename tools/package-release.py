#!/usr/bin/env python3
"""Package the native Build artifacts."""

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
import zipfile


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def common_files(source, destination):
    for name in ("README.txt", "LICENSE"):
        copy_file(source / name, destination / name)
    notices = (source / "THIRD-PARTY-NOTICES.txt").read_text(encoding="utf-8")
    for name in (
        "third_party/pcre2/LICENCE.md",
        "third_party/pcre2/deps/sljit/LICENSE",
        "third_party/libyaml/License",
    ):
        notices += f"\n\n=== {name} ===\n\n"
        notices += (source / name).read_text(encoding="utf-8")
    (destination / "THIRD-PARTY-NOTICES.txt").write_text(notices, encoding="utf-8")


def zip_folder(folder, output):
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(folder.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(folder).as_posix())


def checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    builds = {preset: args.artifacts / f"hdtrans-{preset}"
              for preset in ("windows-x64", "windows-x86", "linux", "mac", "wasm")}
    versions = {(root / "hdtrans-version.txt").read_text(encoding="utf-8").strip() for root in builds.values()}
    if len(versions) != 1:
        parser.error(f"artifact versions differ: {sorted(versions)}")
    version = versions.pop()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+_-]*", version):
        parser.error(f"invalid package version: {version!r}")
    args.output.mkdir(parents=True, exist_ok=True)
    packages = []
    with tempfile.TemporaryDirectory(prefix="hdtrans-package-") as temporary:
        staging = Path(temporary)
        windows = staging / "windows"
        windows.mkdir()
        for name in ("hdtrans.exe", "hdTransGUI.exe", "hdTransEngine.dll"):
            copy_file(builds["windows-x64"] / name, windows / name)
        for name, renamed in (
            ("J2KEngine.dll", "J2KEngine.dll"),
            ("hdTransEngine.dll", "hdTransEngine_x86.dll"),
            ("hdtrans.exe", "hdtrans_x86.exe"),
        ):
            copy_file(builds["windows-x86"] / name, windows / renamed)
        shutil.copytree(builds["windows-x64"] / "hdTrans", windows / "hdTrans")
        common_files(args.source, windows)
        package = args.output / f"hdTrans-{version}-windows.zip"
        zip_folder(windows, package)
        packages.append(package)

        for preset, library in (("linux", "libhdTransEngine.so"), ("mac", "libhdTransEngine.dylib")):
            architecture = (builds[preset] / "ARCH").read_text(encoding="utf-8").strip()
            arch_names = {"X64": "x64", "ARM64": "arm64"}
            if architecture not in arch_names:
                parser.error(f"unsupported {preset} architecture: {architecture}")
            platform = "macos" if preset == "mac" else "linux"
            folder = staging / preset / f"hdTrans-{version}"
            folder.mkdir(parents=True)
            for name in ("hdtrans", library):
                copy_file(builds[preset] / name, folder / name)
            (folder / "hdtrans").chmod(0o755)
            for name in ("dicts", "user"):
                shutil.copytree(builds[preset] / name, folder / name)
            common_files(args.source, folder)
            package = args.output / f"hdTrans-{version}-{platform}-{arch_names[architecture]}.tar.gz"
            with tarfile.open(package, "w:gz", compresslevel=6) as archive:
                archive.add(folder, arcname=folder.name)
            packages.append(package)

    sums = "".join(f"{checksum(path)}  {path.name}\n" for path in sorted(packages))
    (args.output / "SHA256SUMS.txt").write_text(sums, encoding="utf-8")
    print(sums, end="")


if __name__ == "__main__":
    main()
