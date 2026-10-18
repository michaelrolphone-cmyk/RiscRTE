# Accepted STAT observation: new local candidate

This implementation starts from published Runtime `584067ddeb6e3170be25261a99c2c1e7912090ee` (tree `a5687e98eb0c9bdd24468e6f64dec611b7202eae`). It is not byte-identical recovery of the lost unpublished `4d58f201be34f170b4fed55fd6744086804c4caf` base or `bdb9545e58ab776a0cfd4268c305bac71c9d16c6` candidate. Historical receipts do not qualify it. No remote publication or device operation is part of this candidate.

## Scope and interpretation

The observed device request was `RTEF1 3b6e3f8a 5 STAT 12`; its raw reply was `RTEF1 3b6e3f8a 5 -3 STAT 0 0\n`. Namespace listing and PATH success do not assert a RuntimeFiles binding or provider readiness. This narrow observation records existing decisions for a completed, accepted STAT. It does not add provider predicates, refreshes, metadata calls, retries, waits, protocol fields or custody behavior.

`RiscBoot::StatObservation` is a caller-owned 24-byte internal structure: flags; preState, postState, finalState, metadataStatus; stateCalls. `RuntimeFiles::stat(path, out, observation = nullptr)` initializes all absent signed fields to INT32_MIN and never stores the pointer. The adapter copies values from its already executed state/metadata/readiness calls. Ordinary calls omit capture. There is no public ABI suffix, version or size change, and no new requirement on existing providers.

The synchronous endpoint callback is `(uint32_t session, uint32_t sequence, uint64_t namespaceId, int32_t status, const StatObservation&)`. It receives final wire status after the existing retention check. Native composition copies it into a 48-byte record (namespace, decision, session, sequence, status, timestamp) with no paths, payloads, heap allocation or surviving stack pointer. The 96-byte live snapshot contains current and first-retained records. The first-retained record never changes after its first nonzero namespace. Namespace zero is absence, is rejected by namespace admission, and emits no STAT diagnostic line. Polling and disconnect do not clear accepted evidence; start resets it. A cached duplicate does not call the observer or provider again.

Flag bits: Bound=1, RetainedOnEntry=2, MetadataCalled=4, ReadyCalled=8, ReadyTrue=16. The retained-on-entry guard precedes bound(), so flags=2 means binding was not sampled, not that binding was absent. If the guard was reached and failed, flags=0 with no metadata/state calls identifies unbound. State call count and metadata-called distinguish reached results from absent sentinels.

For the exact released Waveshare provider, clean pre-state UNAVAILABLE yields -3 without metadata; active session, sleep prepared and I/O-unavailable intentionally remain the same class here. A successful metadata call followed by UNAVAILABLE yields -3 with pre=READY/metadata=OK/post=UNAVAILABLE. Healthy missing is +1 with metadata=NOT_FOUND, repeated READY and ready=true. Provider/owner ambiguity or failed metadata followed by unavailability retains custody (-6). The new capture does not identify every internal SD predicate. If pre-state UNAVAILABLE is observed on hardware, another separately authorized provider-specific observation would be needed to distinguish its gates.

## Timing and retrieval limits

The timestamp is sampled by the native callback after completed accepted dispatch, from the existing diagnostic clock. Values in the decision were sampled sequentially at their existing branch sites. Existing serial-control/native guard events are separate samples, often before accepted dispatch. These records are not an atomic provider/native snapshot and do not establish a boot-mount or card-liveness timeline.

Replay takes an owned copy when `diag` is accepted. Subsequent live captures do not modify that in-flight dump. Output reuses the existing 64-byte-per-poll transport limit and 192-byte line buffer; measured maximum STAT line is 141 bytes including newline. Current and first-retained each add two lines when present. RTEF1 wire output is unchanged.

No retained/stopped diagnostic-pump framework was reconstructed. If Runtime stops servicing the existing diagnostic path after retention or another stall, having a copied record does not guarantee it can be retrieved. Host reconnect/diag qualification covers a serviced owner path after clean -3, not physical availability after a device stall.

## Fresh local qualification

Released provider: `michaelrolphone-cmyk/RiscRTE-Wavshare-S3-Touch-AMOLED-2.06` at `e1b2fc8b801b33e3bd6ea30626a8dda9bf2237a6`. Tests include its unchanged driver and FatFs on RAM media, with host peripheral fixtures and fault injection. Staged SDK headers follow the published Runtime fixture's source precedence; every selected header and provider input is hashed. There is no physical SD, USB, native worker-task, full-product or device qualification.

- Ten provider cases: unbound, active-session, sleep-prepared, I/O-unavailable, owner-refused, provider-retained, healthy, healthy missing, successful-metadata/post-unavailable, failed-metadata/post-unavailable. Every case passes baseline versus candidate-capture-off versus candidate-capture-on exact wire, state/metadata/ready/transport call counts and retained outcome parity. Cached duplicates issue no new observation/calls. Copied replay after disconnect is checked.
- Focused RuntimeFiles tests: explicit sentinels on all early returns, existing generic ABI prefix, invalid/context/sleep/safety/retained entry, capture omission, no surviving output pointer, generic metadata error mapping, first-retained immutable, maximum line lengths.
- Production diagnostic adapter tests: owner/ISR exclusion, synchronous copy, absent/start reset, disconnect retention, immutable replay, no polling overwrite.
- Native host owner/transport tests: exact request identity; internal and external workspace selections; callback, duplicate, disconnect plus diag, partial TX, owner/ISR, immutable replay and first-retained.
- Existing fresh baseline and candidate suites: 10 serial scenarios; 5,394 storage adapter checks plus C ABI; diagnostic checkpoint and journal suites including pinned Arduino HWCDC; six native diagnostic workspace configurations.

These pass normally and with ASan+UBSan (detect_stack_use_after_return=1 for focused tests). LeakSanitizer is disabled (`detect_leaks=0`) because this executor reports it cannot operate under ptrace; the failed default-LSan run is preserved. Initial native test compilation/sequence mistakes are preserved alongside corrected reruns. Those were test fixture errors, not qualification of the failed versions.

Native host fixtures use only official IDF `esp_err.h` and `esp_compiler.h` from pinned `fff9895c82d744c7237be8847347bdd1b07c6643`, plus repository shims. No SDK/toolchain installation or full target build was performed. No Xtensa compiler was available for new target measurements.

Representative reproduction (from Runtime checkout, sibling paths shown):

```sh
python3 test/run_stat_observation_provider_test.py --provider ../provider --baseline ../baseline --output ../receipts/provider-new
ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=1 python3 test/run_stat_observation_provider_test.py --provider ../provider --baseline ../baseline --output ../receipts/provider-new-san --sanitize
ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=1 SANITIZE=1 bash test/run_stat_observation_unit_test.sh
ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=1 SANITIZE=1 IDF_PATH=../idf-headers bash test/run_stat_observation_native_test.sh
python3 test/measure_stat_observation.py --baseline ../baseline --idf ../idf-headers --output ../receipts/memory-new
```

## Measured host cost versus published base

GCC 14.2.0 x86-64, `-O2`, no exceptions/RTTI; not ESP32 target costs and not a comparison against the lost frozen base.

| Item | Baseline | Candidate | Delta |
|---|---:|---:|---:|
| RuntimeFiles | 200 | 200 | 0 |
| StorageFilesVolumeAdapter | 160 | 160 | 0 |
| Endpoint | 1432 | 1440 | +8 |
| Hooks | 32 | 40 | +8 |
| Native State | 40 | 48 | +8 |
| Native endpoint workspace | 2968 | 2976 | +8 |
| Diagnostic Replay | 2520 | 2616 | +96 |
| Diagnostic workspace, sleep only | 3512 | 3704 | +192 |

Diagnostic-on measured production objects: NativeSerialFiles text +48, internal BSS +8, external BSS +8; Files text +345; SleepDiagnostics text +761, rodata +182, external BSS +192. Other measured data/BSS unchanged. Diagnostic-off SleepDiagnostics object is unchanged; endpoint/callback storage remains in the serial feature. These object deltas are not a link-time flash estimate.

The new observation code was independently reviewed read-only against the published base. Review found no correctness blocker, changed provider call ordering, extra storage predicate, wire/cache change, or borrowed-pointer lifetime issue.
