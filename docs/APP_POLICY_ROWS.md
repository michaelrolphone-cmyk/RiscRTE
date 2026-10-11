# Explicit seventeen-row app policy metadata

Runtime0.1.68 offers a native compile-time option, `RISC_APP_POLICY_ROWS=17`.
The default is16; only16 and17 compile. The option changes only each admitted
`AppPolicy` metadata array. App manifests still allow at most16 distinct
requirements and one app invocation still has exactly16 live capability grants.
It does not add grants, capabilities, namespace sharing, provider selection,
hardware rights, retained identity fields, or public SDK ABI.

One declared KV requirement can cover multiple distinct, explicitly provisioned
positive namespaces. Thus16 requirements may require17 policy rows. Duplicate
namespace rows remain invalid; namespace0 cannot select an ambiguous match.
Every selected app policy must still match its manifest exactly before any app
runs. Failed allocation/admission fails closed and normal destruction frees the
metadata. The existing selected-PSRAM allocation policy has no internal-RAM
fallback.

## Native build and packaging

For a new app-data candidate, use one of these single-job targets:

```
pio run -e esp32s3-16mb-appdata-policy17 -j 1
pio run -e esp32s3-16mb-appdata-iq-policy17 -j 1
```

The matching performance variants are
`esp32s3-16mb-appdata-perf-policy17` and
`esp32s3-16mb-appdata-iq-perf-policy17`. All existing environments retain16.
A central composition with its own environment can add
`-DRISC_APP_POLICY_ROWS=17` to its native Runtime compilation. Apply it consistently
to every translation unit and to native admission validators using that Runtime.
The option belongs to native Runtime, not application or provider ELF flags.

The linked `risc_app_policy_rows` string is
`RISC_APP_POLICY_ROWS:16` or `RISC_APP_POLICY_ROWS:17`. Boot prints the same marker.
`scripts/paired_candidate.py --app-policy-rows 17` selects the opt-in environment
and verifies the exact marker in both ELF and firmware binary, rejects the
opposite marker, and writes `native_proof.app_policy` with the row, live-grant
and manifest-requirement bounds. Existing packaging defaults to16 and verifies
its matching marker. The marker cannot stand in for independent manifest/policy
admission. Composition must select a matching candidate; changing a JSON claim
cannot turn a sixteen-row binary into a seventeen-row binary.

## Layout cost

On Xtensa GCC8.4, each private `AppGrantPolicy` remains24 bytes. Opt-in adds one
such row per admitted app policy; no additional Runtime member or live grant is
allocated. The extra metadata is24 bytes per admitted app (576 bytes at24 apps,
456 at the legacy19-app maximum), in the same selected metadata heap. The default
layout is unchanged. On the 64-bit host the row is32 bytes and the per-app
allocation changes936 to968 bytes; host `sizeof(Runtime)` stays250840 bytes.

## Software coverage and remaining integration

`test/run_key_value_multi_test.sh` exercises omitted, explicit16 and17 builds.
It covers exact17 admission,18 rejection, default17 rejection,16 requirements
with17 rows, a seventeenth requirement rejected even with16 grants, duplicate
KV namespace rejection, distinct namespace isolation, owner checks,16 live-slot
exhaustion, release/reacquire of namespace17, copied stale handles/tables, invalid
option compilation, allocation failure and cleanup, and the last driver/platform
indices. ASan/UBSan applies to the same real host shared-module fixtures.

The demand-retention and retained-wake scripts accept
`RISC_APP_POLICY_ROWS=17`. Their opt-in fixtures fill metadata to17 rows while
keeping sparse working sets. Demand tests cover timer-only zero activation,
late use, handoff, ordinary and legacy graph limits, and failed cleanup retention.
Deep tests place retained-wake in row17 and cover fresh/one-shot wake,
foreign app/cohort identity rejection, owner revocation, corruption and refusal.
Unused KV backends assert if touched. CI runs default and opted-in variants,
including ASan/UBSan. Packaging tests reject missing, conflicting and wrong-row
markers independently in either binary artifact.

These are generic Runtime proofs. Real X4 Contexts Clock peak-grant,
sparse-startup and deep-return parity still require central integration with the
RF-only Contexts service profile and selected X4 app/store composition. This
isolated change does not claim that product proof or physical qualification.
No publication, frozen product mutation, flash or device access is part of it.
