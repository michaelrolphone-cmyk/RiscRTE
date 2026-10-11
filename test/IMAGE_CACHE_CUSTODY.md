# Combined native allocation and stream custody regression

`run_image_cache_pressure_test.sh` includes five combined scenarios in addition
to the original twelve pressure scenarios. It uses the production Runtime,
GraphV2, ModuleV2, AppStreamSessions, ProviderQueueHost, NativeAppMemory,
AppAllocationLedger, ELF import resolver, allocation adapter and native module
registry. The fixtures supply ordinary software app/provider/dependency images;
they do not implement another broker or app lifecycle.

## Cases

- `ledger-final` starts a real session with RX/TX bytes, requests a child and lets
  Runtime close the session and reclaim the returning app's ledger. The next
  `native_app_memory_begin` fails both its initial allocation and its only
  pressure retry when cache is enabled (one allocation attempt when disabled).
  The child never reaches mapping/init/entry. Runtime logs failed-child recovery,
  ends the failed stream invocation and reloads the default with fresh globals,
  a new client context, new session/endpoint handles and a new grant generation.
  The copied old client and grant cannot invoke the provider. Exact ledger
  attempts/failures are asserted: 4/2 with cache, 3/1 without, including both
  successful default invocations.
- `custody-app` invokes the real app `malloc` import through NativeAppMemory and
  its allocation ledger while a live broker session owns two populated queues.
- `custody-provider` invokes the real provider `malloc` import from inside the
  broker's checked provider control callback with the same live queue custody.
- `custody-app-retained` and `custody-provider-retained` repeat the respective
  pressure path, then return a failed provider close with both queues populated.
  Even with demand-retained boot pins, Runtime rejects the copied client,
  revokes grants, cancels a queued child, blocks further polling/cleanup/relaunch,
  skips app fini, and preserves all three mapped images, native allocations and
  78 bytes of reserved queue storage until process exit.

The cache-on allocation fails once and succeeds after reclaiming exactly the
immutable input image and cache metadata. Every reclaim free checks that the
three mappings, queue storage and app/provider sentinel allocations remain live.
Both RX and TX keep their complete info/counter snapshots and exact binary bytes
across pressure, using the original handles. A real provider poll is observed
before the retained-failure no-poll assertion. Healthy exits require zero tracked
native blocks/bytes, zero native mappings and zero queue bytes. Every case also
fills all 16 live app grant slots and rejects acquisition 17, including builds
with explicit 17-row policy metadata.

## Commands and recorded result

On 2026-10-08, all 17 scenarios passed with cache 0 and 1 in each of these four
builds (136 scenario executions):

```sh
bash test/run_image_cache_pressure_test.sh
RISC_APP_POLICY_ROWS=17 bash test/run_image_cache_pressure_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_image_cache_pressure_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 RISC_APP_POLICY_ROWS=17 bash test/run_image_cache_pressure_test.sh
```

The script accepts `PRESSURE_SCENARIOS` for a focused rerun. It changes no
production defaults: cache remains off, metadata rows remain 16 unless 17 is
explicitly selected, and live grants/manifest requirements remain 16.

## Limits

Host mappings execute host shared images; file reading/relocation, allocator and
RTOS backends substitute for target hardware. Queue/session metadata and byte
rings use production `new` paths, outside the pressure harness's redirected
malloc/caps wrappers. These cases prove their custody survives app/provider
pressure; they do not inject failure into those lazy `new` allocations or measure
complete target RAM headroom. Retained queues cannot be read through revoked
public handles; post-fence checks establish retained storage and denial, while
byte-for-byte preservation is checked after pressure and before the close fault.

LeakSanitizer could not scan this executor (fatal ptrace restriction before the
first scenario), so the recorded sanitizer runs disable its leak detector.
ASan and UBSan remain enabled and fail on the first finding. The healthy-path
allocation/mapping/queue accounting is independently asserted. Retained cases
intentionally keep custody alive and never force cleanup. No device access,
firmware build, physical transport qualification, publication or product change
is part of this regression.
