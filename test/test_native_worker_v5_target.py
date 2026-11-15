#!/usr/bin/env python3
"""Synthetic parser/negative fixtures; actual target evidence is a separate audit."""
import copy
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location(
    "target_gate", Path(__file__).resolve().parents[1] / "scripts/native_worker_v5_target.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
SHA = "a" * 40
TREE = "b" * 40


def identity():
    return {"RISC_SOURCE_SHA": SHA, "RISC_CPU_SCOPE_V5_CANDIDATE": "ON", "IDF_TARGET": "esp32s3"}


def elf():
    data = bytearray(256)
    data[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 94, 1, 0x42000000,
                     52, 128, 0, 52, 32, 1, 40, 2, 1)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0, 0x42000000, 0x42000000, 84, 84, 5, 4)
    strings = b"\0.shstrtab\0"
    data[224:224 + len(strings)] = strings
    struct.pack_into("<IIIIIIIIII", data, 168, 1, 3, 0, 0, 224, len(strings), 0, 0, 1, 0)
    return data


class TargetGateTests(unittest.TestCase):
    def check_identity(self, cache=None, head=SHA, sdk=gate.SDK_SHA, generated=None):
        gate.check_identity(head, TREE, SHA, sdk, cache or identity(),
                            generated if generated is not None else '#define RISC_BUILD_SOURCE_SHA "' + SHA + '"\n')

    def test_identity_positive(self):
        self.check_identity()

    def test_wrong_source_and_sdk(self):
        for kwargs in ({"head": "c" * 40}, {"sdk": "d" * 40}, {"generated": ""}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.check_identity(**kwargs)

    def test_stale_build_identity(self):
        data = identity()
        data["RISC_SOURCE_SHA"] = "e" * 40
        with self.assertRaisesRegex(ValueError, "stale build"):
            self.check_identity(data)

    def test_candidate_off_and_wrong_target(self):
        for key, value in (("RISC_CPU_SCOPE_V5_CANDIDATE", "OFF"), ("IDF_TARGET", "esp32s3-wrong")):
            data = identity()
            data[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check_identity(data)

    def test_product_or_internal_selection_refused(self):
        for key in gate.FORBIDDEN:
            data = identity()
            data[key] = "1"
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check_identity(data)

    def test_config_positive_and_parsers(self):
        config = "\n".join(k + "=" + ("y" if v == "1" else v) for k, v in gate.REQUIRED_CONFIG.items())
        header = "\n".join("#define " + k + " " + v for k, v in gate.REQUIRED_CONFIG.items())
        gate.check_config(gate.config_values(config), gate.config_values(header, True))

    def test_every_required_config_rejects_missing_or_disabled(self):
        for key in gate.REQUIRED_CONFIG:
            for absent in (False, True):
                data = dict(gate.REQUIRED_CONFIG)
                if absent:
                    del data[key]
                else:
                    data[key] = "0"
                with self.subTest(key=key, absent=absent), self.assertRaises(ValueError):
                    gate.check_config(data, dict(gate.REQUIRED_CONFIG))

    def test_generated_config_drift(self):
        data = dict(gate.REQUIRED_CONFIG)
        data["CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC"] = "0"
        with self.assertRaises(ValueError):
            gate.check_config(dict(gate.REQUIRED_CONFIG), data)

    def test_smp_refused(self):
        data = dict(gate.REQUIRED_CONFIG, CONFIG_FREERTOS_SMP="1")
        with self.assertRaises(ValueError):
            gate.check_config(data, data)

    def test_required_symbol_positive(self):
        text = "\n".join("42000000 T " + name for name in gate.REQUIRED_SYMBOLS)
        gate.check_final_symbols(gate.parse_nm(text))

    def test_missing_or_unresolved_final_symbol(self):
        data = {name: {"type": "T"} for name in gate.REQUIRED_SYMBOLS}
        for name in gate.REQUIRED_SYMBOLS:
            for typ in (None, "U"):
                changed = copy.deepcopy(data)
                if typ is None:
                    del changed[name]
                else:
                    changed[name]["type"] = typ
                with self.subTest(name=name, typ=typ), self.assertRaises(ValueError):
                    gate.check_final_symbols(changed)

    def test_actual_compile_entry_shape(self):
        source, build = Path("/source"), Path("/build")
        row = {"file": str(source / gate.CANDIDATE[0]), "directory": str(build),
               "arguments": ["cc", "-DRISC_CPU_SCOPE_V5_CANDIDATE=1",
                             "-DRISC_NATIVE_ESP_IDF=1", "-o", "task.obj", "-c", str(source / gate.CANDIDATE[0])]}
        argv, obj = gate.compile_entry([row], source, build, gate.CANDIDATE[0])
        self.assertEqual(obj, build / "task.obj")
        self.assertIn("-DRISC_CPU_SCOPE_V5_CANDIDATE=1", argv)
        for table in ([], [row, row]):
            with self.subTest(table=table), self.assertRaises(ValueError):
                gate.compile_entry(table, source, build, gate.CANDIDATE[0])

    def test_compile_flags_and_external_object_refused(self):
        for extra in ("@hidden.rsp", "-DRISC_NATIVE_PROVIDER_POLICY_HEADER=x", "-DRISC_ELF_INTERNAL_PLACEMENT_V1=1"):
            row = {"file": "/source/" + gate.CANDIDATE[0], "directory": "/build",
                   "arguments": ["cc", "-DRISC_CPU_SCOPE_V5_CANDIDATE=1", "-DRISC_NATIVE_ESP_IDF=1", extra, "-o", "t.obj", "-c", "/source/" + gate.CANDIDATE[0]]}
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                gate.compile_entry([row], Path("/source"), Path("/build"), gate.CANDIDATE[0])
        for flags in (["cc", "-DRISC_CPU_SCOPE_V5_CANDIDATE=0", "-o", "t.obj"],
                      ["cc", "-DRISC_CPU_SCOPE_V5_CANDIDATE=1", "-DRISC_NATIVE_ESP_IDF=1", "-o", "/outside.obj"]):
            row["arguments"] = flags
            with self.assertRaises(ValueError):
                gate.compile_entry([row], Path("/source"), Path("/build"), gate.CANDIDATE[0])

    def test_elf_positive(self):
        self.assertEqual(gate.parse_elf(elf())["load_segments"][0]["file_bytes"], 84)

    def test_wrong_or_truncated_elf(self):
        for data in (b"", elf()[:60], b"NOPE" + elf()[4:]):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                gate.parse_elf(data)
        for offset, value in ((16, 1), (18, 3), (42, 0)):
            data = elf()
            struct.pack_into("<H", data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                gate.parse_elf(data)

    def test_partition_positive(self):
        data = struct.pack("<HBBII16sI", 0x50AA, 0, 0, 0x10000, 0x300000, b"factory", 0) + bytes(32)
        self.assertEqual(gate.check_partition(data, 0x13C200)["free_bytes"], 0x1C3E00)

    def test_missing_or_overfull_partition(self):
        for data, size in ((bytes(32), 1),
                           (struct.pack("<HBBII16sI", 0x50AA, 0, 0, 0x10000, 10, b"factory", 0), 11)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                gate.check_partition(data, size)


    def test_response_real_format(self):
        text = "-mlongcalls\n-fno-builtin-memcpy\n-fno-builtin-memset\n-fno-builtin-bzero\n-mdisable-hardware-atomics\n-fno-rtti\n"
        result = gate.expand_responses(["cc", "@/build/toolchain/cxxflags"], "/build", Path("/build"),
                                       lambda p: text.encode())
        self.assertEqual(result, ["cc"] + text.splitlines())
        self.assertIn("-mdisable-hardware-atomics", result)

    def test_response_escaping_nesting_and_cycles_refused(self):
        for text in ("@cxxflags\n", "@/build/toolchain/cflags\n", '-DNAME="value"\n',
                     "-I\\ escaped\n", "-I/one -I/two\n", ""):
            with self.subTest(text=text), self.assertRaises(ValueError):
                gate.response_tokens(text)
        for token in ("@/outside/cflags", "@/build/toolchain/../../outside", "@/build/other/cflags"):
            with self.subTest(token=token), self.assertRaises(ValueError):
                gate.expand_responses([token], "/build", Path("/build"), lambda p: b"-Os\n")

    def test_forbidden_response_content_is_not_ignored(self):
        for flag in ("-DRISC_ELF_INTERNAL_PLACEMENT_V1=1", "-DRISC_NATIVE_PROVIDER_POLICY_HEADER=x",
                     "-DRISC_CPU_SCOPE_V5_CANDIDATE=0"):
            argv = gate.expand_responses(
                ["cc", "-DRISC_CPU_SCOPE_V5_CANDIDATE=1", "-DRISC_NATIVE_ESP_IDF=1",
                 "@/build/toolchain/cflags", "-o", "t.obj", "-c", "/source/" + gate.CANDIDATE[0]],
                "/build", Path("/build"), lambda p: (flag + "\n").encode())
            row = {"file": "/source/" + gate.CANDIDATE[0], "directory": "/build", "arguments": argv}
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                gate.compile_entry([row], Path("/source"), Path("/build"), gate.CANDIDATE[0])

    def test_recorded_root_identity(self):
        roots = (Path("/source"), Path("/sdk"), Path("/build"), Path("/tools/gcc"))
        data = {"idf_path": "/sdk", "project_path": "/source/idf", "build_dir": "/build",
                "config_file": "/build/sdkconfig", "c_compiler": "/tools/gcc",
                "target": "esp32s3", "project_name": "riscrte_native"}
        gate.check_project(data, *roots)
        for key in ("idf_path", "project_path", "build_dir", "config_file", "c_compiler"):
            changed = dict(data)
            changed[key] = "/different"
            with self.subTest(key=key), self.assertRaises(ValueError):
                gate.check_project(changed, *roots)

    def test_source_operand_identity(self):
        row = {"file": "/source/" + gate.CANDIDATE[0], "directory": "/build",
               "arguments": ["cc", "-DRISC_CPU_SCOPE_V5_CANDIDATE=1", "-DRISC_NATIVE_ESP_IDF=1",
                             "-o", "t.obj", "-c", "/different/source.c"]}
        with self.assertRaisesRegex(ValueError, "source operand"):
            gate.compile_entry([row], Path("/source"), Path("/build"), gate.CANDIDATE[0])

    def test_consumed_include_roots(self):
        roots = (Path("/source"), Path("/sdk"), Path("/build"), Path("/tools"))
        gate.check_include_roots(["cc", "-I/sdk/components/freertos", "-I/build/config"], "/build", *roots)
        for argv in (["cc", "-I/foreign-sdk/components/freertos"], ["cc", "-I/source/include"]):
            with self.subTest(argv=argv), self.assertRaises(ValueError):
                gate.check_include_roots(argv, "/build", *roots)


if __name__ == "__main__":
    unittest.main(verbosity=2)
