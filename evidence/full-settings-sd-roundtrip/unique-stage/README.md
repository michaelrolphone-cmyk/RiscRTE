# Full controller roundtrip on final unique-stage production sources

Both optimized and ASan/UBSan complete-composition runs passed on the exact
production combination:
- `6200421bb49c27d94c6f815239281298b48bc69f`: target-bound stage catalog.
- `7dd80141a25f874d965d47747a7123c534d451bd`: unique durable stage writer.
- `140a57ed3c8289a23ee67e6c3ff0ad3036e01894`: remove unused Recovery-stage
  deletion authority.

The isolated equivalents are `605e619`, `5c627d2`, and `e2232e9`. After both
runs, production and test sources were verified byte-identical to integration
Runtime `3795a60` with:

```
git diff --exit-code 3795a60 -- src sdk experimental test
```

No full-roundtrip test adaptation was needed. Each run covers actual Settings
preference saves and two actual Wi-Fi saved profiles, shared checked native
coordination, unique-stage durable SD publication, complete simulated native
file/NVS erasure, typed checked SD restoration, and fresh actual controllers
and Runtime read-through readers for namespaces 1 and 6.

The second campaign additionally cancels the native acknowledgement after real
SD publication, reconstructs native objects, reloads the selected durable
marker, and requires exact `AcknowledgePrefix` recovery before the full erasure
and restoration. This is a clean interruption boundary, not process-death
power-cut coverage.

All maps match exact pre-checkpoint bytes. The unrelated file remains readable
with successful open/read/close and USB is admitted after terminal release.
Each entire shared SD image remains byte-identical through both typed restores:

- normal/normal: `d50d31bd5bd071b0b6e3d7aa65cadfdf37f39eabbdb57507bf581cf1621d6324`
- normal/interrupted: `d50d31bd5bd071b0b6e3d7aa65cadfdf37f39eabbdb57507bf581cf1621d6324`
- asan-ubsan/normal: `d50d31bd5bd071b0b6e3d7aa65cadfdf37f39eabbdb57507bf581cf1621d6324`
- asan-ubsan/interrupted: `d50d31bd5bd071b0b6e3d7aa65cadfdf37f39eabbdb57507bf581cf1621d6324`

Exact System closures stay pinned separately at Settings `5636756` (75 inputs)
and Wi-Fi `a5bd60f` (63 inputs). Manifests record all Runtime/external hashes,
commands, binary hashes, sanitizer environments, erased-artifact hashes and
complete-image checks. Source hashes are checked again at completion.

ASan/UBSan use leak detection disabled because this host rejects LeakSanitizer
process inspection; leak checking remains unqualified. Initial matching media,
first-export incarnation, UI/radio/RTC providers, NVS SDK and sector hardware
remain explicit synthetic fixtures. No target firmware, physical storage/RTC,
actual Files app, updater delivery or product policy is qualified.
