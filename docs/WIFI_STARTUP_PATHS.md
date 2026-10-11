# Selected X4 Wi-Fi startup investigation

This slice did not reproduce a new production Wi-Fi lifecycle defect. It adds
allocation-pool snapshots to the existing plain stage logger, without changing
SDK configuration, policy, retries, cleanup, ABI or the Runtime 0.1.73 HCI queue.
The qualified 0.1.73 source/version remains frozen; diagnostics use 0.1.74.
A separate, reproduced native app message-classification defect is repaired in
System Wi-Fi 1.1.15 as described below.

## Exact source and selected product

- Runtime baseline: `b587df55298e0bb8e676b3d59ca13679c0267bf7` (0.1.73).
- System Wi-Fi app: `7b175418e3063d9f4c571acf06c366144831b2af`.
- Wi-Fi 0.2.0 provider: Watch `cf30d732271db19a74492d4753736cafeafb92bb`.
- HCI 0.3.0 provider: Watch `4966acee548e6cc3997289db206e996ed4c15cda`.
- Product: X4 `3096c2ae881bfb62627df5614fe94a83c376c66c`,
  `build/complete-021-final/store`: 19 apps, 22 selected providers,
  `provider_activation: demand-retained`. This existing store has Runtime .72;
  its selected app/provider artifacts are measured, not silently relabeled .73.
- Contexts service source: Utilities `1e751d83c93dd5b4be5db9079bcf137a6efa4388`.
  Broadcast service source: Utilities `c0de37886a6ad568fda1520423622fadbd60b7d3`.

The earlier policy runner omitted Quick Radios. The selected product includes
Quick Radios, BLE broadcast (default off), native custody/time, paper preferences,
transitions, touch scrolling, automatic idle and low-battery policy. The new
combined fixture compiles these features together. Its receipt records every
extracted source hash and the exact compiler defines. The firmware identity
string does not affect the exercised controller path.

## Cross-layer behavior

The fixture executes the production controller and adapter, unchanged Wi-Fi/HCI
providers, CpuPort, NativeRadio and NativeHci. The actual Runtime
`providerStorageSafe` and `promotionSafe` checks are evaluated against that port.
UI, time/storage contents, outer grants, telemetry service and IQ PHY operations
are deterministic boundaries. Provider mappings stay live while app init/open,
close/fini and fresh init/open repeat. It does not claim to load the full product
graph or emulate every service, Xtensa execution, an allocator or radio hardware.
Existing native-radio fault cases are reused only as SDK shim definitions; their
old test main is not executed by this combined fixture.

Eight scenarios pass normally and with ASan/UBSan:

1. Twelve app close/reopen cycles, alternating prior Bluetooth OFF/ON, with
   explicit connection and scan. Idle retained Wi-Fi claims survive navigation;
   a healthy Bluetooth controller does not block Wi-Fi. A radio operation blocks
   app exit, while healthy provider storage remains usable.
2. SDK Wi-Fi init refusal cleans up. The app remains live and a later explicit
   Connect succeeds, with no automatic retry.
3. The 600-byte scan allocation fails cleanly before SDK setup. Explicit Scan
   succeeds after the allocation becomes available.
4. A real CPU IQ lease blocks Wi-Fi setup and first-demand promotion; its checked
   release permits explicit Connect. The fixture supplies only the PHY boundary.
5. Wi-Fi stop failure retains the native lease, closes global storage safety and
   retains the app; no cleanup bypass or later normal app I/O is permitted.
6. Broadcast pause failure retains the app before Wi-Fi reaches native setup.
7. A malformed HCI event and failed controller disable close the real global
   storage/first-demand safety checks. The exact Bluetooth token remains owned.
   Checked recovery restores safety, after which explicit Wi-Fi Connect works.
8. An earlier active Wi-Fi operation is explicitly disconnected by the app before
   the next Connect. The idle provider token alone does not make Wi-Fi unavailable.

These are distinct outcomes. `CpuPort::radioJoin`/`radioScanStart` reject an
active or closing Wi-Fi lease and an IQ lease, but not a healthy HCI controller.
Runtime's first demand-retained acquisition uses `promotionSafe`, which also
requires app-exit safety. An unrelated HCI fault can therefore block first
activation. Bound provider storage and native realtime access use the global
storage guard. The app's native custody adapter treats an uncertain external
acquire or failed cleanup as terminal retention, so an apparent unavailable app
need not have reached `esp_wifi_init` at all. The policy/SDK stage boundary
identifies that difference without weakening the safety guard.

The exact Contexts source performs a synchronous configured IQ capture. BUSY
means another owner's refusal, never permission to suspend that owner; only
`RISC_RADIO_IQ_CLEANUP_RETAINED` preserves its cleanup obligation. The selected
Wi-Fi app has no Contexts grant, so it cannot independently poll that service.
Successful navigation also requires the previous app's active radio resources
to be safe. A retained mapping is not evidence of an active IQ operation.

The selected broadcast client pauses advertising before Wi-Fi connect/scan and
before storage. The real service's `pause` closes its telemetry token; a failed
close remains retained. Thus this sequencing can intentionally stop Wi-Fi when
Bluetooth cleanup cannot be established. Its retry policy is not changed here.

## Reproduced native policy-message defect

System source `7b175418` returns the same false boolean for explicit Off,
malformed policy and recoverable policy I/O. The native custody wrapper accepts
I/O as an ordinary error, so Connect/Scan remain live and display the incorrect
`Wi-Fi is off in Quick Controls` message. The new combined fixture fails on
that exact observed message before the change; no Wi-Fi SDK call occurs.

System commit `5513ee25cbe2ff4a51c16cb37242db933d0cb36d` fixes native
classification in the selected X4 Wi-Fi 1.1.15 profile. A single policy read and
checked release return allowed/off/unavailable. I/O or corruption displays
`Radio settings unavailable; retry`, explicit Off retains its original message,
and missing policy still permits Wi-Fi. Already-retained custody returns before
further app work. The existing Watch boolean policy branch stays unchanged.
The added three combined cases check I/O, corruption and Off for both Connect
and Scan, assert exactly one policy read per action, and pass normally and under
ASan/UBSan. This repairs the error explanation, not RF connectivity.

## Measured selected memory and SDK differences

`report_wifi_memory.py` reads the actual selected ELF section tables and applies
Runtime's `esp_elf_data_reserve` padding. It hashes every measured image and the
boot file. These are exact section-allocation requests, not process RSS or a
claim about the device's current free heap.

| Selected allocation | Bytes |
| --- | ---: |
| All 22 providers' text and data reservations | 1,911,498 |
| Wi-Fi app text | 54,648 |
| Wi-Fi app data/rodata/data.rel.ro/BSS reservation | 74,787 |
| Wi-Fi app mapping total | 129,435 |
| Wi-Fi app authorized provider dependency closure | 239,624 |
| Home authorized provider dependency closure | 456,326 |
| Contexts provider mapping | 205,494 |
| Both update provider mappings combined | 1,320,541 |
| Native HCI RX/TX allocation | 5,153 |
| Native scan snapshot allocation | 600 |

All ELF text/data mappings use explicit PSRAM capability in this firmware.
Demand-retained does not load unvisited providers. Provider closure rows are
potential authorized demand, not proof that every listed grant was acquired.
Loader symbols/metadata, temporary ELF file bytes, ordinary app/provider heap,
RTOS stacks and private SDK allocations are additional. The app also allocates
48,000-byte alarm/previous-frame buffers under the selected 800x480 mono display;
those ordinary malloc requests follow the target heap's policy rather than the
ELF section allocator. No internal-memory failure follows from adding the PSRAM
mapping totals.

The installed Arduino 2.0.17 SDK's X4 `qio_opi` configuration enables PSRAM malloc,
tries Wi-Fi/LwIP allocations in PSRAM, prefers internal memory below 4,096 bytes,
and reserves zero internal bytes for malloc. Its default Wi-Fi init has eight
static RX buffers, 32 dynamic RX, eight static TX and 16 cached TX. Runtime takes
that default with NVS disabled. Arduino's ordinary `WiFiGeneric.cpp` path,
whose `useStaticBuffers` defaults false, instead selects four static RX, 32
dynamic RX, zero static TX, 32 dynamic TX and four cached TX. This is a verified
SDK initialization-profile difference, not proof of a physical failure or a
reason to change the buffer policy blindly. Working Watch through this same
Runtime adapter uses the Runtime configuration. A specific external product
may override the Arduino default and must be checked separately.

## Bounded diagnostic change

Only Wi-Fi setup and SDK-failure boundaries query heap state. Existing timestamped
stage lines now include internal free bytes/largest block, PSRAM free/largest
block and the internal DMA-capable largest block, immediately before/after
one-time netif init, default event-loop creation, SDK init/start, before netif
allocation and at returned SDK/allocation failures. Polls add no new log lines.
A delivered `before-*` line without a completion narrows the last observed
setup boundary; the existing logger loss counters still apply.

No heap buffers, per-packet logging, credentials, addresses, hardware retries,
new trace recorder, changed SDK calls or success substitution are introduced.
Stage-disabled tests prove no heap queries or timestamp formatter are retained.
Host heap values are deliberate sentinels for logger verification, not device
measurements. These diagnostics discriminate app-policy/ownership refusal,
returned SDK error, allocation pressure and a call that fails to return. They
cannot establish RF, DHCP, physical power or coexistence reliability.

## Reproduce

```
python3 test/run_wifi_cross_layer_test.py --system /path/to/System-Apps \
  --watch /path/to/Watch --utilities /path/to/Utilities \
  --output build/wifi-cross-layer-normal
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 python3 test/run_wifi_cross_layer_test.py \
  --system /path/to/System-Apps --watch /path/to/Watch \
  --utilities /path/to/Utilities --output build/wifi-cross-layer-sanitized
python3 scripts/report_wifi_memory.py --store /path/to/complete-021-final/store \
  --arduino /path/to/framework-arduinoespressif32 \
  --output build/wifi-cross-layer-normal/selected-memory.json
# Verify the native UI repair; omit --system-ref to reproduce the old failure.
python3 test/run_wifi_cross_layer_test.py --system /path/to/System-Apps \
  --system-ref 5513ee25cbe2ff4a51c16cb37242db933d0cb36d \
  --check-policy-classification --watch /path/to/Watch \
  --utilities /path/to/Utilities --output build/wifi-policy-after
bash test/run_radio_stage_test.sh
pio run -e esp32s3-16mb-appdata-iq-stage -j 1
```

The combined fixture's ASan/UBSan runs disable LeakSanitizer in this executor;
retained scenarios intentionally terminate without destroying owned state.
Target compilation/linking passed against the installed pinned SDK. No device,
serial access, flash, delivery, remote branch write or hardware qualification
occurred. This is diagnostic progress, not a claim that Wi-Fi intermittence is
fixed.
