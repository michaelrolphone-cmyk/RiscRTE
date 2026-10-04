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
