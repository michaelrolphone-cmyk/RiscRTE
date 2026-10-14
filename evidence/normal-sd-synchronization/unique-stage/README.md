# Target-bound durable stage checkpoint

Source dependency equivalents are `aed2fe0`, `7cedb73`, and `6200421`
(catalog/reader/session/stage grammar). This increment uses the strict typed
synchronization catalog and changes only future durable staging names. Recovery adds no stage/page cleanup; abandoned pages/stages are never garbage-collected. Its existing reader can still invoke checked provider rollback recovery when the committed index is absent.

- Actual source-pinned X4/FatFs + validated Runtime/CpuPort + checked native
  coordinator: all 31 cases pass, including actual interrupted A -> native reload
  -> real app edit B -> publish/reload/ack with A's stage and both page sets kept.
- Actual empty/exact-prefix target-page repair, nonprefix/oversize refusal.
- Lower SD tests pass normal and ASan/UBSan, including four sparse directory API
  refusals before marker/I/O and eight page/stage collision cases; 360 legacy
  cancel/lease/restart boundaries and 436 legacy returned-error/power cuts.
- Xtensa legacy/cohort headers compile; permanent Sync object is 15,992 bytes,
  in addition to dynamic catalog/map allocations.

The actual 31-case ASan/UBSan and final durable exhaustive runs are separate
follow-on evidence, not claimed by this checkpoint. Initial oversize-fixture
attempt failed because it requested a single volume write above4096 bytes;
fixture seeding/readback now uses bounded chunks. An earlier discarded recovery
cleanup prototype exposed null directory callbacks in the lower fixture; the
final writer validates directory capability before native marker preparation,
and the lower fixture now supplies copied directory operations.

See `NORMAL_SD_SYNCHRONIZATION.md` for the remaining default-off production
startup/private-store/worker/control-memory/custody binding gates. Host SD sectors
and native NVS calls are synthetic; this is no device or physical reboot claim.
