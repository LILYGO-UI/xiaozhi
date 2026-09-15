#!/usr/bin/env python3
"""Add the missing Xiaozhi dependencies to the project's CM0 BSP 0.1.0 cache."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import sys


PROJECT = Path(__file__).resolve().parents[1]
SYSROOT = PROJECT / ".cache/cm0-bsp/0.1.0/sysroot"
PACKAGES = PROJECT / ".cache/helper-sdk-packages"
# Debian trixie arm64 packages. OpenSSL matches the BSP's 3.5.6 runtime;
# the Opus development and runtime packages must be kept at the same version.
DEPENDENCIES = (
    (
        "openssl/libssl-dev_3.5.6-1~deb13u2_arm64.deb",
        "afdd9c57261502a3a434ba606ddfd1e8fd0f288c663f2dc4f7570ce927867651",
    ),
    (
        "opus/libopus0_1.5.2-2_arm64.deb",
        "1d980e8a4717074805a98dbe07671cb3bee068c25709315f3de836980c629836",
    ),
    (
        "opus/libopus-dev_1.5.2-2_arm64.deb",
        "544425aae285926bf1649a1eb854efa63dea573d098f3781fae39cb3b3e7fd7e",
    ),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for command in ("cmake", "curl", "dpkg-deb"):
        if not shutil.which(command):
            raise RuntimeError(f"Missing {command}; on macOS: brew install cmake dpkg")

    if not (SYSROOT / "usr/share/cm0-bsp/toolchain.cmake").is_file():
        subprocess.run(
            ["cmake", "-P", str(PROJECT / "cmake/cm0-bsp-toolchain.cmake")],
            cwd=PROJECT,
            check=True,
        )
    for library in ("libssl.so.3", "libcrypto.so.3", "libasound.so"):
        if not (SYSROOT / "usr/lib/aarch64-linux-gnu" / library).is_file():
            raise RuntimeError(f"CM0 BSP 0.1.0 is incomplete: missing {library}")

    PACKAGES.mkdir(parents=True, exist_ok=True)
    archives = []
    for relative_url, sha256 in DEPENDENCIES:
        archive = PACKAGES / Path(relative_url).name
        if not archive.is_file() or digest(archive) != sha256:
            partial = archive.with_suffix(".deb.part")
            try:
                subprocess.run(
                    [
                        "curl", "--fail", "--location", "--retry", "2",
                        "--connect-timeout", "15", "--max-time", "180",
                        "https://deb.debian.org/debian/pool/main/o/" + relative_url,
                        "--output", str(partial),
                    ],
                    check=True,
                )
                if digest(partial) != sha256:
                    raise RuntimeError(f"SHA-256 mismatch: {archive.name}")
                partial.replace(archive)
            finally:
                partial.unlink(missing_ok=True)
        archives.append(archive)

    # Verify every archive before modifying the sysroot. This only extracts
    # build inputs into the project cache; it does not install host packages.
    for archive in archives:
        print(f"Extracting {archive.name}", flush=True)
        subprocess.run(["dpkg-deb", "--extract", str(archive), str(SYSROOT)], check=True)
    print("Helper SDK ready. Run: cmake --preset cm0-cross")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"prepare-helper-sdk: {error}", file=sys.stderr)
        sys.exit(1)
