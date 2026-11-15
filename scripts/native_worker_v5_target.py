#!/usr/bin/env python3
"""Audit a completed generic ESP32-S3 candidate build, never build or activate it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys

SDK_SHA = "fff9895c82d744c7237be8847347bdd1b07c6643"
COMPILER_RELEASE = "esp-15.2.0_20251204"
CANDIDATE = tuple("lib/elf_loader/src/" + name for name in (
    "esp_cpu_task_retirement_v1.c", "esp_cpu_task_scope_v1.c",
    "esp_cpu_task_stack_v1.c", "esp_cpu_task_stack_port_v1.c",
    "esp_cpu_worker_v5.c", "esp_cpu_monotonic_v1.c"))
RUNTIME = ("src/bootstrap/Runtime.cpp", "src/runtime/drivers/ProviderGraphV2.cpp",
           "src/runtime/drivers/ProviderModuleV2.cpp")
REQUIRED_SYMBOLS = (
    "risc_cpu_worker_start_v5", "risc_cpu_worker_join_v5", "risc_cpu_worker_release_v5",
    "esp_cpu_task_binding_begin_v2", "esp_cpu_task_binding_commit_v1",
    "esp_cpu_task_binding_cancel_staged_v1", "esp_cpu_task_binding_safe_v1",
    "esp_cpu_task_binding_release_v1", "esp_cpu_task_scope_create_held_v1",
    "esp_cpu_retirement_poll", "pthread_internal_local_storage_destructor_callback")
REQUIRED_CONFIG = {
    "CONFIG_IDF_TARGET": '"esp32s3"', "CONFIG_IDF_TARGET_ARCH": '"xtensa"',
    "CONFIG_IDF_TARGET_ESP32S3": "1", "CONFIG_SPIRAM": "1",
    "CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION": "1",
    "CONFIG_ESP_SYSTEM_MEMPROT": "1", "CONFIG_ESP_SYSTEM_MEMPROT_PMS": "1",
    "CONFIG_ESP_SYSTEM_MEMPROT_PMS_LOCK": "1", "CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC": "1",
    "CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS": "1",
    "CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS": "1",
    "CONFIG_ELF_LOADER_LOAD_PSRAM": "1"}
FORBIDDEN = ("RISC_PRODUCT_PROFILE", "RISC_NATIVE_PROVIDER_POLICY_HEADER",
             "RISC_ELF_INTERNAL_PLACEMENT_V1")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def config_values(text, header=False):
    result = {}
    for line in text.splitlines():
        if header:
            match = re.fullmatch(r"#define (CONFIG_\w+) (.+)", line)
        else:
            match = re.fullmatch(r"(CONFIG_\w+)=(.*)", line)
        if match:
            value = match[2]
            result[match[1]] = "1" if value == "y" else value
    return result


def check_config(config, header):
    for key, value in REQUIRED_CONFIG.items():
        require(config.get(key) == header.get(key) == value, "configuration mismatch: " + key)
    require(config.get("CONFIG_FREERTOS_SMP", "0") == "0", "SMP profile unsupported")


def cache_values(text):
    return {m[1]: m[2] for line in text.splitlines()
            if (m := re.fullmatch(r"([^#/][^:]*):[^=]+=(.*)", line))}


def check_identity(head, tree, source_sha, sdk_head, cache, generated):
    require(bool(re.fullmatch("[0-9a-f]{40}", source_sha)), "source SHA must be exact")
    require(head == source_sha and bool(re.fullmatch("[0-9a-f]{40}", tree)), "source identity mismatch")
    require(sdk_head == SDK_SHA, "SDK identity mismatch")
    require(cache.get("RISC_SOURCE_SHA") == source_sha, "stale build source")
    require(cache.get("RISC_CPU_SCOPE_V5_CANDIDATE") == "ON", "candidate option not ON")
    require(cache.get("IDF_TARGET") == "esp32s3", "wrong target")
    require(not any(key in cache for key in FORBIDDEN), "not a generic default-internal-port build")
    expected = '#define RISC_BUILD_SOURCE_SHA "' + source_sha + '"'
    require(expected in generated.splitlines(), "stale generated build identity")


def parse_nm(text):
    result = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) in (2, 3) and len(fields[-2]) == 1:
            result[fields[-1]] = {"type": fields[-2],
                                  "address": fields[0] if len(fields) == 3 else None}
    return result


def check_final_symbols(symbols):
    for name in REQUIRED_SYMBOLS:
        require(symbols.get(name, {}).get("type") == "T", "missing retained text symbol: " + name)


def parse_elf(data):
    require(len(data) >= 52 and data[:7] == b"\x7fELF\x01\x01\x01", "not ELF32 little endian")
    fields = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = fields
    require(kind == 2 and machine == 94 and version == 1 and ehsize == 52, "not Xtensa executable")
    require(phsize == 32 and 0 < phnum <= 64 and phoff + phnum * phsize <= len(data), "invalid program headers")
    require(shsize == 40 and 0 < shstr < shnum <= 4096 and shoff + shnum * shsize <= len(data), "invalid section headers")
    headers = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shsize) for i in range(shnum)]
    names = headers[shstr]
    require(names[4] + names[5] <= len(data), "invalid section names")
    strings = data[names[4]:names[4] + names[5]]
    sections = []
    for row in headers:
        require(row[0] < len(strings), "invalid section name")
        end = strings.find(b"\0", row[0])
        require(end >= 0, "unterminated section name")
        if row[1] != 8:
            require(row[4] + row[5] <= len(data), "section outside file")
        sections.append({"name": strings[row[0]:end].decode("ascii"), "type": row[1],
                         "flags": row[2], "address": row[3], "size": row[5]})
    segments = []
    for i in range(phnum):
        typ, off, va, pa, filesz, memsz, flg, align = struct.unpack_from("<IIIIIIII", data, phoff + i * phsize)
        require(off + filesz <= len(data) and filesz <= memsz, "segment outside file")
        if typ == 1:
            segments.append({"address": va, "physical_address": pa, "file_bytes": filesz,
                             "memory_bytes": memsz, "flags": flg, "alignment": align})
    require(segments, "no load segments")
    return {"entry": entry, "load_segments": segments, "allocated_sections":
            [x for x in sections if x["flags"] & 2],
            "note": "Segments include overlapping dummy/address reservations; do not sum them as RAM. Static sections are not runtime peak heap/stack."}


def check_partition(data, image_bytes):
    rows = []
    for offset in range(0, len(data) - 31, 32):
        magic, typ, subtype, start, size, label, flags = struct.unpack_from("<HBBII16sI", data, offset)
        if magic != 0x50AA:
            break
        rows.append({"type": typ, "subtype": subtype, "offset": start, "bytes": size,
                     "label": label.split(b"\0")[0].decode("ascii"), "flags": flags})
    apps = [x["bytes"] for x in rows if x["type"] == 0]
    require(apps and image_bytes > 0 and image_bytes <= min(apps), "image missing or exceeds app partition")
    return {"partitions": rows, "image_bytes": image_bytes, "smallest_app_bytes": min(apps),
            "free_bytes": min(apps) - image_bytes}


def response_tokens(text):
    """The pinned IDF generator writes one plain compiler option per line."""
    tokens = text.splitlines()
    require(tokens and all(re.fullmatch(r"-[A-Za-z0-9_=.,:+/-]+", x) for x in tokens),
            "unsupported generated response-file syntax")
    return tokens


def expand_responses(argv, directory, build, read):
    expanded = []
    for token in argv:
        if not token.startswith("@"):
            expanded.append(token)
            continue
        path = Path(token[1:])
        if not path.is_absolute():
            path = Path(directory) / path
        require(not path.is_symlink(), "symlink response file")
        path = path.resolve()
        require(path.parent == build / "toolchain" and path.name in ("cflags", "cxxflags", "asmflags"),
                "response file is not the owning generated toolchain input")
        data = read(path)
        require(len(data) <= 65536, "response file too large")
        expanded.extend(response_tokens(data.decode("utf-8")))
    return expanded


def check_project(project, source, sdk, build, compiler):
    for key, expected in (("idf_path", sdk), ("project_path", source / "idf"),
                          ("build_dir", build), ("config_file", build / "sdkconfig"),
                          ("c_compiler", compiler)):
        require(key in project and Path(project[key]).resolve() == expected.resolve(),
                "recorded build root mismatch: " + key)
    require(project.get("target") == "esp32s3" and project.get("project_name") == "riscrte_native",
            "wrong recorded project/target")


def check_include_roots(argv, directory, source, sdk, build, compiler_root):
    includes = []
    index = 0
    while index < len(argv):
        token = argv[index]
        if token in ("-I", "-isystem", "-iquote"):
            index += 1
            require(index < len(argv), "missing include argument")
            includes.append(argv[index])
        elif token.startswith("-I") and len(token) > 2:
            includes.append(token[2:])
        index += 1
    resolved = []
    for value in includes:
        path = Path(value)
        path = (path if path.is_absolute() else Path(directory) / path).resolve()
        require(any(path.is_relative_to(root) for root in (source, sdk, build, compiler_root)),
                "include outside recorded source/SDK/build/toolchain: " + str(path))
        resolved.append(path)
    require(any(x.is_relative_to(sdk / "components") for x in resolved), "missing recorded SDK includes")


def compile_entry(table, source, build, relative):
    expected = source / relative
    matches = [x for x in table if Path(x["file"]).resolve() == expected]
    require(len(matches) == 1, "missing/duplicate compile entry: " + relative)
    row = matches[0]
    argv = row.get("arguments") or shlex.split(row["command"])
    require(not any(x.startswith("@") for x in argv), "unexpanded compile response file")
    require(argv.count("-c") == 1 and argv.index("-c") + 1 < len(argv), "missing source operand")
    operand = Path(argv[argv.index("-c") + 1])
    operand = (operand if operand.is_absolute() else Path(row["directory"]) / operand).resolve()
    require(operand == expected, "compile source operand mismatch")
    for macro in ("RISC_CPU_SCOPE_V5_CANDIDATE", "RISC_NATIVE_ESP_IDF"):
        require(not any(x == "-U" + macro or (x.startswith("-D" + macro + "=") and x != "-D" + macro + "=1")
                        for x in argv), "conflicting candidate/native macro")
    require("-DRISC_CPU_SCOPE_V5_CANDIDATE=1" in argv and "-DRISC_NATIVE_ESP_IDF=1" in argv,
            "candidate/native compile flags missing: " + relative)
    require(not any(any(word in x for word in FORBIDDEN) for x in argv), "unexpected compile selection")
    require("-o" in argv and argv.index("-o") + 1 < len(argv), "missing object output")
    obj = Path(argv[argv.index("-o") + 1])
    if not obj.is_absolute():
        obj = Path(row["directory"]) / obj
    obj = obj.resolve()
    require(obj.is_relative_to(build), "object outside explicit build root")
    return argv, obj


def audit(args, report):
    source, sdk, build = (Path(x).resolve() for x in (args.source_root, args.sdk_root, args.build_dir))
    compiler = Path(args.cc).absolute()
    report.update(source_root=str(source), build_root=str(build), sdk_root=str(sdk),
                  source_sha=args.source_sha, sdk_sha=SDK_SHA, artifacts={}, commands=[])
    env = {key: os.environ[key] for key in ("HOME", "PATH", "SystemRoot") if key in os.environ}
    env.update(LC_ALL="C", LANG="C")
    def command(argv, binary=False):
        p = subprocess.run([str(x) for x in argv], capture_output=True, env=env, timeout=120)
        report["commands"].append({"argv": [str(x) for x in argv], "rc": p.returncode,
                                   "stdout_sha256": sha(p.stdout), "stderr": p.stderr.decode(errors="replace")[-2000:]})
        require(p.returncode == 0, "command failed: " + str(argv[0]))
        return p.stdout if binary else p.stdout.decode()
    def pin(path):
        path = Path(path)
        require(path.is_file() and not path.is_symlink(), "missing or symlink input: " + str(path))
        data = path.read_bytes()
        report["artifacts"][str(path)] = {"bytes": len(data), "sha256": sha(data)}
        return data
    git = lambda root, *a: command(["git", "-C", root, *a]).strip()
    head, tree = git(source, "rev-parse", "HEAD"), git(source, "rev-parse", "HEAD^{tree}")
    require(not git(source, "status", "--porcelain=v1", "--untracked-files=normal"), "source is not clean")
    sdk_head = git(sdk, "rev-parse", "HEAD")
    require(not git(sdk, "status", "--porcelain=v1", "--untracked-files=no"), "SDK tracked source drift")
    cache = cache_values(pin(build / "CMakeCache.txt").decode())
    generated = pin(build / "esp-idf/runtime/RiscBuildIdentity.h").decode()
    check_identity(head, tree, args.source_sha, sdk_head, cache, generated)
    project = json.loads(pin(build / "project_description.json"))
    check_project(project, source, sdk, build, compiler)
    report["recorded_roots"] = {key: project[key] for key in ("idf_path", "project_path", "build_dir", "config_file", "c_compiler")}
    report["source_tree"] = tree
    config = config_values(pin(build / "sdkconfig").decode())
    header = config_values(pin(build / "config/sdkconfig.h").decode(), True)
    check_config(config, header)
    report["checked_configuration"] = {key: config[key] for key in REQUIRED_CONFIG}
    report["cache_selection"] = {key: value for key, value in cache.items() if key.startswith("RISC_") or key == "IDF_TARGET"}
    pin(sdk / "tools/tools.json")
    pin(compiler)
    version = command([compiler, "--version"])
    require(COMPILER_RELEASE in version and command([compiler, "-dumpfullversion"]).strip() == "15.2.0", "compiler release mismatch")
    report["compiler_version"] = version
    tools = {}
    for name in ("nm", "ar"):
        tool = Path(command([compiler, "-print-prog-name=" + name]).strip())
        require(tool.is_absolute(), "compiler did not resolve " + name)
        pin(tool)
        tools[name] = tool
    table = json.loads(pin(build / "compile_commands.json"))
    log = pin(Path(args.build_log).resolve()).decode()
    ninja_log = pin(build / ".ninja_log").decode()
    # No hidden product or internal-port selection in any compile command.
    for row in table:
        raw_argv = row.get("arguments") or shlex.split(row["command"])
        argv = expand_responses(raw_argv, row["directory"], build, pin)
        row["arguments"] = argv
        bad = [x for x in argv if any(word in x for word in FORBIDDEN)]
        require(not bad, "non-generic compilation: " + row["file"] + ": " + repr(bad))
    report["translation_units"] = {}
    archives = {}
    for component in ("riscrte_loader", "runtime", "pthread"):
        path = build / "esp-idf" / component / ("lib" + component + ".a")
        pin(path)
        archives[component] = (path, command([tools["ar"], "t", path]).splitlines())
    candidate_symbols = set()
    scope_symbols = {}
    for relative in CANDIDATE + RUNTIME:
        argv, obj = compile_entry(table, source, build, relative)
        require(Path(argv[0]).resolve() == compiler.resolve() or
                (relative in RUNTIME and Path(argv[0]).name.endswith("g++") and Path(argv[0]).parent == compiler.parent),
                "unexpected compiler in compile entry")
        check_include_roots(argv, build, source, sdk, build, compiler.parent.parent.resolve())
        pin(Path(argv[0]))
        source_bytes, obj_bytes = pin(source / relative), pin(obj)
        component = "riscrte_loader" if relative in CANDIDATE else "runtime"
        archive, members = archives[component]
        require(members.count(obj.name) == 1, "object missing/duplicated in archive: " + relative)
        require(sha(command([tools["ar"], "p", archive, obj.name], True)) == sha(obj_bytes),
                "archive object differs: " + relative)
        obj_rel = obj.relative_to(build).as_posix()
        require(any("Building " in line and obj_rel in line for line in log.splitlines()), "no build execution line: " + relative)
        require(any(len(parts := line.split("\t")) == 5 and parts[3] == obj_rel for line in ninja_log.splitlines()),
                "no completed Ninja record: " + relative)
        syms = parse_nm(command([tools["nm"], "-g", obj]))
        if relative in CANDIDATE:
            candidate_symbols.update(k for k, value in syms.items() if value["type"] in ("T", "D", "B", "R"))
        if relative.endswith("esp_cpu_task_scope_v1.c"):
            scope_symbols = syms
        report["translation_units"][relative] = {"source_sha256": sha(source_bytes), "object": str(obj),
            "object_sha256": sha(obj_bytes), "archive": str(archive), "archive_member": obj.name,
            "compiled_execution_record": True, "archive_bytes_match": True,
            "command_sha256": sha(json.dumps(argv).encode())}
    elf = pin(build / "riscrte_native.elf")
    report["elf"] = parse_elf(elf)
    symbols = parse_nm(command([tools["nm"], "-g", build / "riscrte_native.elf"]))
    check_final_symbols(symbols)
    destructor = "pthread_internal_local_storage_destructor_callback"
    require(scope_symbols.get(destructor, {}).get("type") == "U", "missing expected resident pthread reference")
    pthread, members = archives["pthread"]
    require(members.count("pthread_local_storage.c.obj") == 1, "pthread destructor member missing")
    pthread_syms = command([tools["nm"], "-A", pthread])
    require(any(":pthread_local_storage.c.obj:" in line and " T " + destructor in line for line in pthread_syms.splitlines()),
            "SDK pthread destructor definition missing")
    report["required_retained_symbols"] = {key: symbols[key] for key in REQUIRED_SYMBOLS}
    report["candidate_retained"] = sorted(k for k in candidate_symbols if symbols.get(k, {}).get("type") in ("T", "D", "B", "R"))
    report["candidate_not_retained"] = sorted(candidate_symbols - set(report["candidate_retained"]))
    demangled = command([tools["nm"], "-C", build / "riscrte_native.elf"])
    for name in ("RiscBoot::Runtime::startBootProvider(", "RiscBoot::Runtime::closeBootProvider("):
        require(any(" T " in line and name in line for line in demangled.splitlines()), "Runtime caller not retained: " + name)
    report["runtime_callers_retained"] = True
    pin(build / "riscrte_native.map")
    image = pin(build / "riscrte_native.bin")
    partitions = pin(build / "partition_table/partition-table.bin")
    report["image"] = check_partition(partitions, len(image))
    size_line = re.search(r"riscrte_native.bin binary size (0x[0-9a-f]+) bytes. Smallest app partition is (0x[0-9a-f]+) bytes. (0x[0-9a-f]+) bytes \(\d+%\) free.", log)
    require(size_line and tuple(int(v, 16) for v in size_line.groups()) ==
            (len(image), report["image"]["smallest_app_bytes"], report["image"]["free_bytes"]),
            "owning build size check missing/inconsistent")
    report["owning_size_check"] = size_line[0]
    internal = pin(source / "lib/elf_loader/include/private/esp_elf_internal_v1.h").decode()
    require("#define RISC_ELF_INTERNAL_PLACEMENT_V1 0" in internal.splitlines(), "default internal-port selection changed")
    adapter = pin(source / "lib/elf_loader/src/esp_elf_adapter.c").decode()
    require("bool esp_elf_internal_supported_v1(void) { return false; }" in adapter, "adapter availability contract changed")
    pin(source / "src/runtime/drivers/SelectedNativeProviderPolicyV1.h")
    require(not git(source, "status", "--porcelain=v1", "--untracked-files=normal"), "source changed during audit")
    require(not git(sdk, "status", "--porcelain=v1", "--untracked-files=no"), "SDK changed during audit")
    for path, record in report["artifacts"].items():
        require(sha(Path(path).read_bytes()) == record["sha256"], "input changed during audit: " + path)
    report.update(runtime_activation=False, internal_port_available=False, physical_qualified=False,
                  evidence_scope="generic candidate target compile/archive/final-link and static footprint only")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("source-root", "source-sha", "sdk-root", "build-dir", "build-log", "cc", "output"):
        p.add_argument("--" + name, required=True)
    args = p.parse_args()
    report = {"schema": 1, "status": "failed"}
    try:
        require(not Path(args.output).exists(), "output already exists")
        audit(args, report)
        report["status"] = "passed"
    except (ValueError, OSError, subprocess.SubprocessError, KeyError, struct.error, UnicodeError) as exc:
        report["error"] = str(exc)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        print(json.dumps(report, sort_keys=True), file=sys.stderr)
        return 1
    output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "output": str(output),
                      "sha256": sha(output.read_bytes()), "error": report.get("error")}))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
