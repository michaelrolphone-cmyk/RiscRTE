#!/usr/bin/env python3
"""Compile unmodified, hash-pinned ESP LittleFS + LittleFS with host boundaries.

Set ESP_LITTLEFS_SOURCE and LITTLEFS_SOURCE to verified source directories for
an offline run. The former accepts a full checkout or a flat evidence directory.
Without them, exact pinned files are fetched into a temporary build directory.
SANITIZE=1 enables ASan+UBSan; SANITIZERS can select a supported subset.
This host flash-model test neither builds firmware nor qualifies hardware.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
import urllib.request

ESP_COMMIT = "41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317"
LFS_COMMIT = "f53a0cc961a8acac85f868b431d2f3e58e447ba3"
INPUTS = (
    ("esp", "src/esp_littlefs.c", "esp_littlefs.c", "f26aa8bdc70c44e9676069a8d91e11921c770dceac989bbf63d9af73c95dcaeb"),
    ("esp", "include/esp_littlefs.h", "esp_littlefs.h", "f321f41845da8cc68f6c15a860311afe37a52ad57abb95949b820a92e9e8d9ad"),
    ("esp", "src/littlefs_api.h", "littlefs_api.h", "cc806a02463db1516ed1a144189bc9267168b6cd79b543e95fb12099eeed18c2"),
    ("esp", "src/littlefs_esp_part.c", "littlefs_esp_part.c", "fe89617c72ab3d36ea833a58a8cb8e4208509dd32b4663f91d72ba02ea13e0ea"),
    ("lfs", "lfs.c", "littlefs/lfs.c", "0b9845f350c33aa448a916847d2246b76c8e296eed8e7e296ae0430242cfda6c"),
    ("lfs", "lfs.h", "littlefs/lfs.h", "a8c8d70f0863fbbc46ce17c5dc7673b40f1b3c9e7e10f3bf33fd28f03dc67703"),
    ("lfs", "lfs_util.c", "littlefs/lfs_util.c", "f2fbde533670560434bd9f5a547174cc7c5a4670a02c47b4bd85180dced8b2ec"),
    ("lfs", "lfs_util.h", "littlefs/lfs_util.h", "03e912a6e9894c9d10c61f5da22b89ebe0bb778af67972d7b67a5f160731bf72"),
)


def obtain_inputs(destination: Path, esp_source: str | None, lfs_source: str | None) -> None:
    for family, upstream, relative, expected in INPUTS:
        source_dir = esp_source if family == "esp" else lfs_source
        if source_dir:
            candidates = (Path(source_dir) / upstream, Path(source_dir) / Path(upstream).name)
            source = next((candidate for candidate in candidates if candidate.is_file()), None)
            if source is None:
                raise SystemExit(f"Missing pinned source {upstream} in {source_dir}")
            payload = source.read_bytes()
        else:
            repository, commit = ("joltwallet/esp_littlefs", ESP_COMMIT) if family == "esp" else ("littlefs-project/littlefs", LFS_COMMIT)
            url = f"https://raw.githubusercontent.com/{repository}/{commit}/{upstream}"
            for attempt in range(3):
                try:
                    with urllib.request.urlopen(url, timeout=30) as response:
                        payload = response.read()
                    break
                except OSError:
                    if attempt == 2:
                        raise
                    time.sleep(attempt + 1)
        actual = hashlib.sha256(payload).hexdigest()
        if actual != expected:
            raise SystemExit(f"Pinned vendor hash mismatch: {relative}; expected {expected}, got {actual}")
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(payload)
        print(f"PIN {relative} {actual}", flush=True)


def run(arguments: list[str]) -> None:
    print("+ " + shlex.join(arguments), flush=True)
    subprocess.run(arguments, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esp-source", default=os.environ.get("ESP_LITTLEFS_SOURCE"))
    parser.add_argument("--littlefs-source", default=os.environ.get("LITTLEFS_SOURCE"))
    parser.add_argument("--test-source", type=Path, help="Optional separate fixture smoke test; default is the profile test")
    parser.add_argument("--keep-build", type=Path, help="Copy build evidence here after a successful run")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    stubs = root / "test/native_littlefs_profile_stubs"
    sanitize = []
    if os.environ.get("SANITIZE", "0") == "1":
        sanitize = ["-fsanitize=" + os.environ.get("SANITIZERS", "address,undefined"),
                    "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-g"]
    with tempfile.TemporaryDirectory(prefix="native-littlefs-profile-") as temporary:
        build = Path(temporary)
        vendor = build / "vendor"
        obtain_inputs(vendor, args.esp_source, args.littlefs_source)
        common = ["-D_GNU_SOURCE", "-DLFS_NO_DEBUG", "-DLFS_NO_WARN", "-DLFS_NO_ERROR", "-pthread", *sanitize]
        c_flags = ["-std=c11", *common, "-include", str(stubs / "sdkconfig.h"),
                   "-I" + str(stubs), "-I" + str(vendor), "-I" + str(vendor / "littlefs")]
        objects = []
        for relative in ("littlefs/lfs.c", "littlefs/lfs_util.c", "littlefs_esp_part.c", "esp_littlefs.c"):
            obj = build / (Path(relative).name + ".o")
            run([os.environ.get("CC", "cc"), *c_flags, "-c", str(vendor / relative), "-o", str(obj)])
            objects.append(str(obj))
        fixture = build / "fixture.o"
        run([os.environ.get("CC", "cc"), *c_flags, "-Wall", "-Wextra", "-Werror", "-Wpedantic",
             "-c", str(root / "test/native_littlefs_profile_fixture.c"), "-o", str(fixture)])
        objects.append(str(fixture))
        test = args.test_source or root / "test/native_littlefs_profile_test.cpp"
        cpp_includes = [root / "test/native_radio_shim", root / "test/native_nvs_shim", stubs, root, root / "src", root / "sdk/app",
                        root / "sdk/driver", root / "sdk/hardware", root / "test", vendor, vendor / "littlefs"]
        cpp_flags = ["-std=c++17", *common, "-DRISC_STAGE_LOGS=0", "-Wall", "-Wextra", "-Werror",
                     "-Wno-unused-function", *("-I" + str(path) for path in cpp_includes)]
        wrapped = []
        if args.test_source is None:
            appdata = build / "AppDataFiles.o"
            run([os.environ.get("CXX", "c++"), *cpp_flags, "-DESP_PLATFORM", "-c",
                 str(root / "src/runtime/storage/AppDataFiles.cpp"), "-o", str(appdata)])
            objects.append(str(appdata))
            functions = ("stat", "fstat", "open", "read", "write", "close", "fsync", "mkdir",
                         "unlink", "rename", "opendir", "readdir", "closedir")
            wrapped = ["-Wl," + ",".join("--wrap=" + function for function in functions)]
        run([os.environ.get("CXX", "c++"), *cpp_flags, str(test), *objects, *wrapped,
             "-o", str(build / "test")])
        run([str(build / "test")])
        if args.keep_build:
            shutil.copytree(build, args.keep_build, dirs_exist_ok=True)
        print("PASS: pinned ESP LittleFS VFS host flash-model qualification; no hardware claim", flush=True)


if __name__ == "__main__":
    main()
