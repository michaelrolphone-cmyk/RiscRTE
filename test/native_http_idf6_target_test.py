#!/usr/bin/env python3
"""Compile the HTTPS feature with the pinned SDK and configured target flags only."""
import argparse
import json
import shlex
import subprocess
from pathlib import Path

PIN = "fff9895c82d744c7237be8847347bdd1b07c6643"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--idf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    idf = args.idf.resolve()
    assert subprocess.check_output(["git", "-C", str(idf), "rev-parse", "HEAD"], text=True).strip() == PIN
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    entries = [entry for entry in json.loads(args.compile_commands.read_text())
               if entry["file"].endswith("/NativeHardware.cpp")]
    assert len(entries) == 1
    entry = entries[0]
    source = output / "native-http.cpp"
    source.write_text('''#include "ports/esp32s3/NativeHttp.h"
static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(6,1,0));
#if !CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC || defined(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) || defined(CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC)
#error Native HTTPS requires the PSRAM-only SDK startup policy
#endif
extern "C" const risc_http_client_v1* native_http_target_check() {
    RiscCpu::NativeTlsMemory::initialize();
    return RiscCpu::NativeHttp::api();
}
''')
    for stages in (0, 1):
        name = "native-http-stages-" + str(stages)
        command = entry.get("arguments") or shlex.split(entry["command"])
        command = list(command)
        command[command.index("-o") + 1] = str(output / (name + ".o"))
        command[command.index("-c") + 1] = str(source)
        for flag, extension in (("-MF", ".d"), ("-MT", ".o")):
            if flag in command:
                command[command.index(flag) + 1] = str(output / (name + extension))
        # Also works while the main build is waiting to regenerate its public
        # dependencies; always use the SDK's real opaque-layout private header.
        command += ["-I" + str(idf / "components" / relative)
                    for relative in ("esp-tls", "esp-tls/private_include", "http_parser")]
        command += ["-URISC_STAGE_LOGS", "-DRISC_STAGE_LOGS=" + str(stages)]
        result = subprocess.run(command, cwd=entry["directory"], text=True, capture_output=True)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        (output / (name + "-command.json")).write_text(json.dumps(command, indent=2) + "\n")
        assert result.returncode == 0, result.stdout + result.stderr
    print("Native HTTPS: pinned ESP32-S3 target compilation passed with stage logging off/on")


if __name__ == "__main__":
    main()
