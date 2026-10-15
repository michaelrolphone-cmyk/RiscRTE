# Private native driver ABI 2

This is the separately qualified ESP32-S3 native SDK cohort, not a change to
ordinary application imports or the public provider descriptor ABI. Both the
public provider descriptor and this private OS/CPU inventory currently use the
number 2; they are independent contracts.

## Fixed cohort

- ESP-IDF 6.1 source: `fff9895c82d744c7237be8847347bdd1b07c6643`
- Espressif Xtensa GCC: `esp-15.2.0_20251204`, compiler version `15.2.0`
- Cohort: `esp32s3-idf6.1-gcc15.2.0-20251204-v1`
- Boundary profile: `privileged_native_cohort_v2.def`, 33 fields; canonical
  sorted compact JSON SHA-256
  `c880590cd8c5ba64f091d5bef7b2499353755bca582f15533509c638e8efb544`

Resident target compilation checks the SDK, target, compiler, SDK PSRAM atomic
workaround, dual-core non-SMP FreeRTOS configuration, tick/notification/mutex
controls, scalar and handle sizes, portMUX size/alignment/member offsets/free
sentinel, logging value layout/configuration and S3 cache assumptions. The
producer compiles the same profile against its actual native SDK headers.
Product partition tables, storage/settings choices and the full sdkconfig hash
are provenance, not requirements for identical product configurations.
An ABI-relevant product configuration change fails compilation and requires
explicit requalification; it cannot silently reuse this cohort.

The supplied FreeRTOS/HAL header layouts are not compatible merely because
function names still link. Legacy private ABI 1 providers (including SDK4 USB
PHY/HCD/portMUX consumers) are rejected on native builds before privileged
mapping. Conversely, legacy builds reject private ABI 2. The native
`vPortEvaluateYieldFromISR` compatibility bridge does not establish legacy
private-layout compatibility. Ordinary application ELF admission continues to
use its existing, separately qualified libc boundary.

## Frozen inventories and authority

The ABI 1 inventory and frozen USB policy generator remain byte-for-byte
unchanged. ABI 2 has 36 private names, five existing libc names, and three
console substitutions available only with explicitly selected diagnostic ABI 1.
This is the exact 44-name optimized USB candidate closure. The six additions
are `_frxt_setup_switch`, `esp_log`, `esp_rom_delay_us`, `periph_rcc_enter`,
`periph_rcc_exit`, and `xTaskGetCoreID`. The earlier unoptimized candidate also
imported `xTaskGenericNotify`; the qualified `-Os` closure does not, so ABI 2
does not include it. All remaining unused ABI 1 names also stay out of ABI 2.
The RCC imports refer to the resident shared RCC lock, not a provider-local
copy. HAL structures remain internal to the rebuilt provider.

There is no fixed-wide ABI 2 admission entry point. Trusted firmware selects
exact canonical imports for one task-owned, module-bound, one-shot relocation.
Symbols are unavailable before/after that relocation; nested, replayed,
wrong-task and wrong-module attempts fail. The selected resolver cannot fall
back to customer registrations or another loaded ELF. Explicit v1/v2 lookup
functions never return the other inventory. The existing v1-named ownership
and relocation hooks version the shared scope mechanism, not the import set.

`NativeProviderPolicyV1` is a private firmware metadata structure. Its existing
`osCpuAbi` field selects the cohort; its schema name is not an import ABI.
Admission checks cohort, identity, version, requirements, exact byte length,
SHA-256, entry symbol and every undefined symbol in `.dynsym` and `.symtab`.
The public provider descriptor is checked after mapping and before activation;
the fixed-candidate generator also audits its on-disk descriptor.
The graph also compares `requiredOsCpuAbi` with the trusted policy explicitly.
Owned copies and fresh hash/import checks remain mandatory before relocation.
Failed starts and incomplete quiescence retain code, dependency pins and
callbacks under the existing lifecycle contract.

Authority comes from the firmware's trusted compiled selection. A sidecar,
qualification receipt, JSON ABI claim, self-declared hash or arbitrary import
list cannot grant privileges. Low-level verified entry points are mechanisms;
they do not authenticate a package themselves. A separate native policy
generator validates fixed producer evidence before emitting metadata for
reviewed firmware inclusion. Default selection remains empty.

The native USB candidate requires only `board.power.vbus@1`. It does not acquire
`platform.usb.phy.resource`, so the old two-dependency policy is not reused.
This ABI qualification does not establish hardware resource coexistence,
physical operation, or a memory sandbox for native code.

## Verification

Host tests execute production structural/import validation, resolver/scopes,
owned graph/module admission, diagnostic sink composition, bank provisioning
and staged admission. Both resident cohorts are tested, including wrong-cohort
rejection before allocations, forged/mutated policy/images, hidden weak imports,
missing diagnostic components, failure retention and repeated cleanup.

`test/native_private_abi2_target_test.py` compiles small production loader objects
with real pinned SDK/GCC flags, checks strong private symbol references and the
33-field profile, and requires seven deliberately changed configurations to
fail. An optional final firmware ELF checks that every private name has a
resident definition. It does not execute Xtensa code or qualify hardware.

The policy generator and exact candidate evidence are documented separately in
[USB_CONTROLLER_IDF61_POLICY.md](USB_CONTROLLER_IDF61_POLICY.md).
