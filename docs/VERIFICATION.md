# Software verification and hardware boundary

The initial migrated slice was checked locally in the isolated checkout. Its
follow-up at `5612d5dddc1341ff79619f1cc8c563598b84730c` also passed exact-head cloud
integration [run37096319352](https://github.com/michaelrolphone-cmyk/RiscRTE/actions/runs/37096319352).
Later-head results belong in the PR/check records; this file does not imply that
an earlier pass certifies a newer commit.

Passed locally and in that cloud run:

- Real host ELF default → child → fresh default → missing child → fresh default,
  typed driver lifecycle and exact-format advancing heartbeat from shipped source.
- Inherited graph/ownership/grant/cycle/failed-start/dependency-retention/recovery
  regressions. Linux ASan/UBSan runs passed. The earlier local macOS sanitizer
  stall remains an environment-specific interrupted run, not a Linux failure.
- All eight pinned Watch profiles and all sixteen selected driver instances in
  their real manifests pass typed materialization and DAG admission. Missing
  native providers fail; no Watch hardware execution is inferred from preflight.
- The real Watch I2C driver is mapped twice from identical source bytes, with two
  wiring maps, independent static state and scoped raw transport contexts. Same
  address leases remain independent; failed quiescence retains the first image
  while the second continues and shuts down separately.
- Namespace/physical-alias conflicts, absent-reset timing consistency, reserved
  console pin rejection, inactive bus keys, strict JSON/UTF-8 and mapping errors.
- Five real Xtensa ET_DYN builds: heartbeat default, handoff, child, typed probe
  and real Watch I2C. Inherited structural validator checks malformed/truncated
  sections/symbols/relocations; allocation ledger checks repeated exit/overflow.
- ESP32-S3 baseline firmware and SPIFFS image, exact-source release stage/verify,
  image checksums/digests, partition layout and extracted boot-store file bytes.
- Version/release immutability guards and pinned fixture provenance checks.

The three canonical Garden profiles are additionally pinned and exercised by the
board suite; the isolated initial tests used their earlier read-only source copies.
No Garden product functionality is migrated.

The CAM candidate build/custody check passed in
[run37096319425](https://github.com/michaelrolphone-cmyk/RiscRTE/actions/runs/37096319425).
Its separate hardware job ended **failure** after receiving the trusted external
`ESP32-CAM hardware / heartbeat cleanup` failure status. This is not a verified
boot, advancing heartbeat or successful cleanup. Hardware evidence and adapter
installation remain with the sole lab controller owner.

Local target sizes at that source: baseline221224B static RAM /377469B program;
CAM221320B static RAM /351765B program, application binary352128B. The CAM
application embeds only the separately linked2624B default ELF and two JSON files.
All target builds use one job and a task-local PlatformIO core. No serial/flash,
release publication, merge, source working-tree edits or unrelated task jobs were
performed by this migration task.

Limits: local host modules are Mach-O; Linux CI uses host ELF. Xtensa relocation
and PSRAM/cache behavior need actual target evidence. Complete Watch native raw
providers, PMU/RF electrical policy enforcement, attached-storage bootstrap,
privileged OS/CPU admission and normal storage-volume capability services are not
implemented here. Peripheral ELFs remain external files. Only minimal platform
bootstrap and generic runtime/loader code belong to the firmware.


## Generic CPU port and external clock checkpoint

The generic native GPIO/I2C/SPI/clock backend now builds for ESP32-S3 with the
pinned SDK. Local static RAM238896B/program425185B. Clock integration against
Watch commit `aa7b03c15a59aa99b2d60ae20905edbdf62e35e6` passes through actual
Runtime/Graph/CpuPort and five actual dynamically loaded drivers plus the clock
app. Both115200-byte SPI-reconstructed frames match the actual renderer's valid
and unset fixtures; there are no RTC time writes, and bad PMU identity/SPI
failure roll back with no live pins/controllers. See [CPU_PORT.md](CPU_PORT.md)
for the precise physical-model boundary. Physical Watch execution is unrun.

Minimal X4 runtime plus heartbeat ELF at `6a7f7821` passed owner hardware status
`X4 hardware / runtime heartbeat cleanup`:22 advancing candidate heartbeats,
application readback match, protected flash unchanged, approved baseline cleanup
and readback plus5 heartbeats. Its software integration run37097188948 and X4
candidate run37097188954 passed. Later `be6efce` hardware run37098107114 failed
both jobs; X4 timed out without exact-source proof, so no firmware regression or
hardware pass is inferred. The6a7f7821 result is strictly source-specific.

## Station radio candidate, 0.1.9

The local radio suite passes normal and ASan/UBSan runs, including the actual
NativeRadio shim, CpuPort authority/cleanup, full JSON/manifest admission and
twelve real Runtime/Graph/dlopen app-provider lifecycle cases, including
a separate bound-storage provider polling during healthy RF and retained
state/address/scan failures. Leak sanitizer was
disabled for this suite because this executor rejects ptrace-based LSan; this is
not a leak-sanitizer qualification. Existing I2S, deep/timed sleep, Runtime,
retained-app, board and Watch host suites pass; Python custody/release checks
pass with the existing pinned esptool 4.11.0/pyelftools 0.32 test dependencies.
A separate provider-graph sanitizer runner forces leak detection and is blocked
by that same ptrace restriction.

The local single-job `pio run -e esp32s3 -j 1` attempt stopped during prerequisite
installation after the pinned RISC-V toolchain download failed its checksum.
No integrity check was bypassed. No target compilation pass is claimed here;
exact-head CI is required to establish target-build status. No real radio,
network association, credentials, device, flash, merge or release was used.

Hosted exact-head candidate `52b085c2` subsequently passed host/version checks and
baseline firmware/frozen-artifact generation. Its 16 MiB native-USB target failed
linking with a 120-byte `dram0_0_seg` overflow. The scan-session allocation repair
recovers 592 static bytes by pinned Xtensa size-only measurement; a new full
hosted target result is required. It adds OOM/lifetime regression coverage and
does not change SDK Wi-Fi buffer policy or claim sufficient dynamic heap.

## PDM RX prerequisite, firmware0.1.12

The I2S suite now includes input-only DATA cleanup faults, I2S0 mono PDM
configuration, whole-read deadlines/partial copies, direction/owner/core
checks, and22 real Runtime/Graph/dlopen TX/RX lifecycle cases. The latter
exercise actual bound-KV get/put while healthy audio is active, permanent
revocation after unsafe storage access, sleep rejection, failed-open retention,
cleanup retries, and no app fini/unload/child launch behind a live token.
Normal and ASan/UBSan I2S runs pass locally; leak detection is disabled because
this executor rejects LSan under ptrace. Existing normal board, runtime, radio,
light/deep sleep, KV/multi-KV/bound-KV, lease, retained-app, Watch and graph suites
pass. All six target app/provider fixture ELFs compile and pass ELF validation;
the eight Python custody/release checks pass with the existing pinned test venv.
The local Arduino framework SDK is absent, so firmware target compilation has
not run and remains an exact-head CI requirement. No dependency installation,
hardware access, recording or audio output occurred. See [the RX contract](I2S_RX.md).
No hardware qualification is claimed.

## Native module-name regression (Firmware0.1.13)

The owner reported Alarm0.6.5 and Wi-Fi1.1.0 boot failures at
`alarm-service: elf-open-failed`, before the default Clock started. The first
software provider used the same `driver.elf` basename as the already loaded
hardware providers. GraphV2 selected ordinary `dlopen` for software; the native
registry rejected that basename even though its full path and package differed.
OS-only host `dlopen` tests used different lookup semantics and missed the error.

`test/run_native_registry_test.sh` now compiles the unmodified production
`dlfcn.c` and `dlmod.c`, with host ELF relocation behind that registry. It first
reproduces the collision and preserves ordinary duplicate rejection, then checks
mixed hardware/software same-basename graph nodes, software singleton admission,
repeated acquisition/reference counts, distinct state, fresh reload, revoked
lease generations, failed relocation retry, retained failed start/dependencies,
verified recovery, cleanup/fini and corrupt same-basename rejection. Normal and
ASan/UBSan runs pass locally. The existing dependency-lifetime and bound-storage
suites now inspect actual independent mappings; retained replacement uses the
exact same paths and checks fresh contexts and rejection of old authority.

The host backend executes native host fixtures, not Xtensa instructions. It does
not establish target relocation/cache correctness or physical display behavior.
Exact-source hosted native builds and a paired Watch store/Clock gate are still
required for a candidate, and physical qualification remains the owner's step.
