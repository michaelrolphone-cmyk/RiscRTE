#!/usr/bin/env python3
"""Compile the selected native entry point/transport against an existing IDF build; no device I/O."""
import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("compile_commands", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
entries = json.loads(args.compile_commands.read_text())
entry = next(row for row in entries if row["file"].endswith("/src/main.cpp"))
original_root = str(pathlib.Path(entry["file"]).parents[1])
command = entry.get("arguments") or shlex.split(entry["command"])
flags = []
i = 1
while i < len(command):
    arg = command[i]
    if arg in ("-o", "-MF", "-MT", "-MQ"):
        i += 2
        continue
    if arg in ("-c", "-MD", "-MMD", "-MP") or arg == entry["file"] or arg.startswith("-DRISC_NATIVE_SERIAL_FILES="):
        i += 1
        continue
    flags.append(arg.replace(original_root, str(root)))
    i += 1
if "-DRISC_NATIVE_ESP_IDF=1" not in flags:
    raise SystemExit("Use a selected native ESP-IDF compile database")
commands = []
for enabled in (0, 1):
    for relative in ("src/main.cpp", "src/bootstrap/Runtime.cpp", "src/ports/esp32s3/CpuPort.cpp",
                     "src/ports/esp32s3/NativeSerialFiles.cpp", "src/ports/esp32s3/SleepDiagnostics.cpp",
                     "src/runtime/serial/Files.cpp"):
        source = root / relative
        output = out / (source.stem + "-" + str(enabled) + ".o")
        cmd = [command[0], *flags, "-DRISC_NATIVE_SERIAL_FILES=" + str(enabled),
               "-c", str(source), "-o", str(output)]
        commands.append(cmd)
        subprocess.run(cmd, cwd=entry["directory"], check=True)
(out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
nm = str(pathlib.Path(command[0]).with_name(pathlib.Path(command[0]).name.replace("g++", "nm")))
assert not subprocess.check_output([nm, str(out / "NativeSerialFiles-0.o")], text=True).strip()
print("Selected native main/Runtime/CpuPort/Console integration compile, feature on/off, empty default-off adapter PASS")
