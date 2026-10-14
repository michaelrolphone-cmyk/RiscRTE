# Checked catalog, provenance and terminal-custody union

This disconnected source checkpoint is based on published Runtime
`ab515c0c89b3a069c5de6546d2c28c760d73012b`, tree
`c487e4ff74b9f1b91d60fbd7ea46917728d736c4` (the bounded SD catalog). It joins the
qualified root-bound provenance checkpoint `67cb33bb9d52807b3abd2ccb377dbd76c8a897dc`
and terminal-custody checkpoint `7daed93b1837c1418531c9c95630136afb0d4ed7`.
The only merge conflict was the combined target probe: both catalog functions and
layout arrays, and the admission factory/initializer probe, are preserved.

The implementations keep their documented contracts:

- A catalog Report is historical checked-close evidence, never a reusable
  creation/adoption permit. Missing indices alone cannot authorize publication.
- RTENR03 binds the complete native map and its 96-byte baseline provenance to one
  selected root. Unknown Unclassified/ImportedLegacy state refuses pending-only
  adoption; only explicitly admitted FreshEmpty can use that existing path.
- FreshEmpty admission is native-only and entropy-free. An SD catalog/card is not
  an initialization dependency. Native rows/journals/banks still block freshness.
- Final post-end RETAINED observation outranks success, cancellation and previous
  errors for replica/legacy readers and writers. Recovery inherits reader custody.
- Known-base durable markers preserve captured-prefix authority after reboot and
  newer edits. First creation, imported-baseline disposition, automatic recovery
  of interrupted FreshEmpty initialization, product routing and hardware approval
  remain separate gates. No SDK/UI/.90/production source changes are included.

## New integration cases

Eight cases extend `native_settings_catalog_test.cpp`, using the real admission
factory and Core with a synthetic native backend/inventory and the existing
copied-worker SD fixture. Each reader and catalog is either constructed before
native initialization (header unavailable) or after RTENR03 is selected. At start
it must use the live selected header, not a constructor-cached v1 assumption.

- At native capacity 65,512, replica and paged-legacy canonical maps of exactly
  65,028 bytes validate. Maps of 65,029 bytes report CapacityExceeded.
- An independently valid single-file legacy slot remains Valid when the other
  two sources exceed their budgets. No capacity failure becomes vacancy.
- V1 single-file canonical payloads of 52 bytes honor an exact 52-byte RTENR03
  budget; a 51-byte budget refuses them, despite v1's different fixed source size.
- The default catalog inventory remains 36,864 bytes (host ABI); v3's largest
  child allocation is 65,160 bytes (65,028 plus 132-byte legacy source envelope).
  Peak tracked allocation is therefore 102,024 bytes in those full-map cases.
- A full-budget report/candidate is still insufficient to adopt when original
  native pending groups consume additional capacity. The generic receiver refuses
  NoSpace before staging; the root, map, pending intent and provenance survive.
- Every observation and refused admission preserves the exact native root and
  object bytes, pending edits and source files. All test values are synthetic.

The published catalog fault suite remains intact, including 1,016 asynchronous
cancel/removal/export/restart boundaries, 604 directory/file/recovery/close faults
and 504 absence/empty/artifact-only faults. The joined terminal-custody suite
includes the 130-case strict post-end observation regression.

## Reproduction

Host dependencies: C++17/C compiler, OpenSSL development headers/library and the
clean pinned System behavior at `c564dd665072db141fc42d55be3be52511750719`. Published
System audit `5a1941e4d0d85fbd9c50604ad4869eb798d05476` adds audit/evidence files but
does not alter any existing Wi-Fi Stage, PortableApps header or SHA2 source used
here. The actual app-owned Stage and full-chain runners retain their exact
behavior pin; no test silently rebinds to the audit head.

Run all thirteen affected suites normally, then under UBSan and ASan/UBSan:

```sh
export SYSTEM_ROOT=/path/to/clean/System-c564dd665072db141fc42d55be3be52511750719
export BUILD_ROOT=/tmp/native-union-catalog NO_PIE=1
suites='native_replica_core native_replica_export_marker native_rebase_candidate
native_rebase_wifi_stage native_replica_store native_replica_sd
native_replica_sd_reader native_replica_legacy_reader native_baseline_admission
native_rebase_roundtrip native_durable_export native_export_retained
native_settings_catalog'
for suite in $suites; do bash "test/run_${suite}_test.sh"; done
export SANITIZE=1 SANITIZERS=undefined
for suite in $suites; do bash "test/run_${suite}_test.sh"; done
```

The ASan runs use non-PIE binaries, including older standalone runners without a
NO_PIE option. Create a disposable compiler wrapper for those runners:

```sh
wrapper=$(mktemp)
printf '%s\n' '#!/usr/bin/env bash' \
  'exec "${HOST_CXX:-c++}" -fno-pie -no-pie "$@"' > "$wrapper"
chmod +x "$wrapper"
export CXX="$wrapper" ASAN_OPTIONS=detect_leaks=0 SANITIZE=1
export SANITIZERS=address,undefined
for suite in $suites; do bash "test/run_${suite}_test.sh"; done
rm "$wrapper"
unset CXX
```

LeakSanitizer is not qualified in this hosted environment. Strict compile-only
Xtensa checks are serial, use no exceptions/RTTI and treat warnings as errors:

```sh
PACKAGES=/path/to/pinned/platformio-core/packages
bash test/compile_native_replica_target.sh "$PACKAGES" /tmp/union-target
bash test/compile_native_export_marker_target.sh "$PACKAGES" /tmp/union-target
TARGET_CXX="$PACKAGES/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++" \
  bash test/compile_native_rebase_target.sh
"$PACKAGES/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm" -S --size-sort \
  /tmp/union-target/native-replica-target-probe.o | grep catalog_layout
```

No firmware image is linked, installed or flashed by these commands. Target
compilation does not qualify physical media, actual entropy or a live owner lease.

## Executed result, 2026-10-11

All thirteen suites passed normally, under UBSan, and under ASan/UBSan with leak
detection disabled. This includes the full catalog fault suite and eight new v3
integration cases, actual checked native adapter and Wi-Fi Stage roundtrips,
post-reboot durable-prefix recovery, and all 130 terminal-custody cases. No
sanitizer diagnostic occurred.

All three strict target commands passed with Xtensa ESP32-S3 GCC 8.4.0
(`esp-2021r2-patch5`). Combined replica/catalog/admission probe SHA-256:
`e071c0cb452ee7bca54c0d519053b378db0e3e2c5bc0b1b85908a67c25bd8a86`.
Export-marker probe SHA-256:
`d9ee661dd2aaf149c6bf06a369d716d358a36bb116f4afd637c92b407133b246`.
The preserved target layout arrays report catalog object 14,496 bytes, Report
464 bytes, inventory entry 144 bytes and child storage 9,376 bytes. These are
object-layout observations, not measured whole-system or physical-device usage.

The union makes no further core/provenance/transport implementation changes
beyond the two qualified checkpoints. New changes are the combined target probe,
catalog integration tests and this proof document. Production, SDK and .90 files
remain byte-identical to the published catalog base.
