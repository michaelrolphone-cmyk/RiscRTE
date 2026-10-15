# Experimental native-IDF6 USB controller policy

`generate_usb_controller_idf61_policy.py` emits an explicitly selected trusted
firmware policy for the separately built native ESP32-S3 controller. The frozen
Reader `generate_usb_controller_policy.py`, its 47-import inventory, ABI-1
policy and tests are unchanged. The experimental candidate has its own identity:

- Driver: `usb-controller-esp32s3`, version `0.1.25`
- ELF: `792696` bytes, SHA-256
  `9de7e76d83bd984c76263b863de715cc5058d6fc21387fb728c5aa3a1441e294`
- Architecture/target: `xtensa-esp32s3` / `esp32s3`
- Private OS/CPU ABI: `2`; diagnostic ABI: `1`
- Cohort: `esp32s3-idf6.1-gcc15.2.0-20251204-v1`
- Public provider descriptor: ABI `2`, exactly `40` bytes
- Provision: `usb.controller@1`
- Sole dependency: `board.power.vbus@1`

The public provider descriptor ABI and private OS/CPU ABI are independently
versioned. Their coincident numeric value `2` does not make them interchangeable.
The generated header uses the existing internal `NativeProviderPolicyV1` POD;
that schema name does not force `osCpuAbi=1`.

## Exact build evidence

The generator independently pins the complete ELF digest, length and exact
sorted 44-import inventory. Both ELF symbol tables must carry that inventory.
A single mapped global `t5_driver_get` export and the public descriptor identity,
size, interface and lifecycle/diagnostic callback mappings are inspected in the
ELF itself. The inventory's newline-terminated source file has SHA-256
`a21d1ca8d39b47b994068074f668ce7c71f8a8247711681ad4cf8bcfc9bf395e`.

The required producer `qualification.json` must agree with these pinned inputs:

- Official ESP-IDF commit `fff9895c82d744c7237be8847347bdd1b07c6643`
- Official esp-usb commit `0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a`, USB `1.6.0`
- Compiler `15.2.0`, distribution `esp-15.2.0_20251204`
- Reviewed `-Os` optimization and the exact 33-field ABI-relevant profile,
  SHA-256 `c880590cd8c5ba64f091d5bef7b2499353755bca582f15533509c638e8efb544`
- Schema `1`, experimental/not-hardware-qualified status, identity, target,
  architecture, OS/CPU ABI, public provider ABI, cohort and capability graph
- Exact ELF hash, byte count, sorted import list and reviewed USB configuration
- Explicit SDK configuration and compile-command hashes, each consistently
  recorded once in `source_hashes`, plus the pinned import-file hash
- Completed loader-map audit with `1078` relocation sites and no unmapped
  relocations, relative values or executable sections, plus the exact seven
  audited absolute peripheral relocation records

Whole SDK configuration and compile-command hashes are provenance and internal
consistency checks, not a requirement that unrelated product settings or
partition choices share identical whole-file bytes. JSON booleans and floating
point values cannot stand in for integer ABI/API/version fields. Duplicate JSON
keys, missing required evidence, added dependencies, inventory drift and a
changed controller all fail before the output header is written.

The separate profile pins FreeRTOS core/tick/notification/allocation choices,
public SDK scalar and handle sizes, stack alignment, critical-section lock
layout and constants, logging configuration layout/macros, and HAL cache-level
constants. The producer compiles target assertions for all 33 values together
with its SDK signature probe. The generator requires both those exact values
and their canonical JSON digest; replacing a value and recomputing a sidecar
digest cannot authorize a new cohort. The canonical digest uses sorted keys
and separators `(',', ':')`, with no trailing newline.

Build receipts and source hashes are not signatures or activation authority.
The generator does not trust a sidecar's asserted digest in place of its own
compiled-in pin, and it does not fetch or execute the listed source paths.
Revising the accepted controller, import set, version or ABI/cohort requires a
separate reviewed change to the generator and the corresponding runtime cohort.

The optimized candidate's 44 imports omit `xTaskGenericNotify`, which is present
in the earlier 45-import debug candidate. Neither that debug ELF nor the frozen
47-import Reader ELF is accepted by this generator.

## Generate and check

Run from the Runtime repository using Python with `pyelftools` installed:

```sh
python3 scripts/generate_usb_controller_idf61_policy.py \
  --controller ../usb-native-check/driver.elf \
  --qualification ../usb-native-check/qualification.json \
  --relative-elf-path drivers/usb-controller-esp32s3/driver.elf \
  --output /tmp/usb-controller-idf61-policy.h

python3 scripts/generate_usb_controller_idf61_policy.py \
  --controller ../usb-native-check/driver.elf \
  --qualification ../usb-native-check/qualification.json \
  --relative-elf-path drivers/usb-controller-esp32s3/driver.elf \
  --check /tmp/usb-controller-idf61-policy.h
```

`--relative-elf-path` is the exact runtime-relative selected ELF identity. It is
bounded and rejects absolute paths, traversal, invalid basenames and C++ string
injection. `--check` compares the complete deterministic header byte for byte.
The qualification producer records its original build artifact as `driver.elf`;
the trusted firmware selection supplies its authorized runtime-relative path.

Generation and check mode never modify build-selection files. An authorized
trusted firmware build must explicitly supply
`RISC_NATIVE_PROVIDER_POLICY_HEADER` pointing to the generated header. No
manifest, boot JSON, package or qualification sidecar can set that selection.
The normal selection remains empty. This change neither activates USB nor adds
`platform.usb.phy.resource` to the candidate's dependency graph.

Physical VBUS/role admission, ownership, successful quiescence, retained cleanup,
product selection, and board/hardware qualification remain separate requirements.
An offline check, host test or target link does not establish physical operation.

## Focused verification

```sh
python3 -m unittest discover -s test -p test_usb_controller_idf61_policy.py -v
IDF61_CONTROLLER_ELF=../usb-native-check/driver.elf \
IDF61_QUALIFICATION=../usb-native-check/qualification.json \
  python3 -m unittest discover -s test -p test_usb_controller_idf61_policy.py -v
```

The offline suite uses a small synthetic ELF to exercise parser rejection paths;
that ELF cannot pass the pinned-image gate. It covers path bounds, exact imports,
public descriptor structure, evidence identity/type failures, duplicate/oversized
JSON, source-hash consistency, loader-map failures, output fields and preservation
of an existing header on invalid input. The opt-in real-artifact test validates
exact generation/check mode and rejects byte changes at the beginning, middle
and end. A small host C++ check compiles the emitted header, verifies that native
ABI-2 selection is valid, that legacy builds reject it, and that the default
selection remains empty. No target builds, device execution, flashing or
publication are performed by these tests.
