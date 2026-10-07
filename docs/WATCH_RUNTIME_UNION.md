# Watch/shared Runtime union (0.1.54)

This source combines two public Runtime inputs without changing either input:

- Watch USB recovery: `3fde7198deffd21b263666a86f58eab2c6eadbc7`, Runtime 0.1.53
- Shared GPIO, lifecycle and time work: `30dcec5ce6ce33223f2b203a2399283e1f758567`, Runtime 0.1.52

The test-only IQ admission fixture correction from public
`b514a540acb404913a704be38ce4901d57d43ca2` is also included. It supplies
counted prepare/cleanup callbacks and adds absent-callback rejection cases; no
production provisioning behavior is changed.

Both runtime inputs descend from the accepted Watch IQ lifecycle baseline
`4a3493a2f76a4e83d28782c644713830baaa346e`. The shared input already includes
that Watch baseline; a larger version number alone did not imply that the
Watch-only 0.1.53 branch contained the intervening shared features.

## Reconciliation

All 38 non-overlapping files changed by the Watch USB commit are preserved
byte-for-byte, including production recovery, tests, pinned HWCDC sources and
provenance. The only production overlap is in `src/main.cpp` and
`src/ports/esp32s3/NativeHardware.cpp`. Those files retain the entire shared
input, with exactly four diagnostics conditionals upgraded from
`RISC_SLEEP_DIAGNOSTICS` to `RISC_DIAGNOSTIC_ADAPTER`.

This preserves native retained-wake/realtime initialization, their Deep-entry
wrappers and the scoped GPIO/PWM changes, while allowing owner-polled USB
recovery when the journal is opted out. There is one provider-delay poll hook;
the shared input's existing diagnostic poll is upgraded, not duplicated.
`platformio.ini` records the separately reserved union version 0.1.54.

The union also preserves the shared input's additive file dispatch, selected
provider synchronization, GPIO output readback, held-output retirement, cached
GPIO writes, native PWM accounting, retained-wake and realtime capabilities,
demand activation, default-only provider promotion, invocation retention and
readonly provider-time broker. Their contracts and focused tests remain in the
corresponding documents and test scripts. Existing manifests retain eager
activation when no new activation policy is selected. New capabilities still
require their explicit existing admission/authority checks.

No product manifest, source lock, application policy, shared hardware ABI,
partition geometry, installer transport or previously delivered image is
changed by this source union. Product integration must pin the newly published
exact source and build fresh artifacts; the union is not a relabel of 0.1.53.

## Verification boundary

The source reconciliation is checked with host regressions for USB diagnostics,
Light/Deep sleep, retained wake, canonical/provider realtime, held GPIO/PWM,
provider synchronization, invocation retention, demand/promotion, ordinary app
lifecycle, bound storage, cohort admission, Watch profiles, IQ lifecycle,
file dispatch, startup failure, native registry, provider graph/leases and
bootloader-backed native provisioning admission (27 scenarios).
Focused ASan/UBSan variants and the frozen legacy Runtime ABI proof on both
host and Xtensa are also run. These are software checks, not hardware evidence.

An isolated single-job `esp32s3-16mb-appdata-iq` compatibility build links the
combined source, including the reserved 64 KiB IQ bank and alias exclusion.
It predates the final source/version commit and is not a distributable artifact.
An exact-source native rebuild after publication is required for product use.
Physical USB enumeration, sleep/wake reliability, current and display behavior
remain unqualified by these checks.
