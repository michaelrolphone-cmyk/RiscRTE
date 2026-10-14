# Attached automatic maintenance with the actual BLE controller

This is separate from the nine explicit-coordinator groups. It compiles
`RISC_NATIVE_SETTINGS_LIFECYCLE=1` and attaches the actual Lifecycle,
Maintenance and typed Bootstrap controllers to the same Runtime running the
actual BLE controller. The required Runtime fence/sweep source is
`c441eaf6d421e2cf690c7e04ce0f53733612fc90` after `3e52de9` and `0e6a322`.

## Source-bound campaign

1. Explicitly seed matching generation-19 native/SD fixture baselines for
   namespaces 1 and 6, plus an unrelated Files-style file.
2. Hold real USB volume export while the actual BLE alias controller saves
   `Kitchen`. Its native commit remains pending because SD maintenance cannot
   admit during USB custody. End USB and prove the old group is still pending.
3. Invoke actual BLE radio enable. Its first atomic grant reaches the settled
   pre-capture checkpoint and acknowledges that old group. The controller then
   captures the current revision and acquires HCI through the actual Runtime
   provider graph. Assert the revision and SD read/write counters do not change
   across HCI acquisition while the atomic draft grant is live.
4. A synthetic physical enable failure drives the actual receipt-bound DELETE
   compensation. The real last-atomic-release boundary synchronizes those two
   pending groups. Repeat enable successfully and require its last release to
   acknowledge the new value before any test helper can trigger maintenance.
5. Save the complete SD sector image and both complete native maps. Delete all
   native files and the simulated NVS witness. In a new process with empty NVS,
   actual Lifecycle hydration uses the typed Bootstrap/checked SD restore path.
   Compare both complete maps and invoke fresh actual BLE alias/radio readers.
   The entire SD image must remain byte-identical across restoration.

The unchanged unrelated file supports real volume open/read/close; USB export
admission works after terminal custody. No atomic storage double is substituted. The HCI provider is a test DSO loaded by Runtime using frozen board
instance 16; only its physical effects are synthetic. SD uses the pinned actual
X4/FatFs driver and frozen .99 board SD9 -> power1 graph. NVS SDK calls, worker
scheduling, internal-memory classification, power-ready, text entry and hardware
sectors are explicit host seams. The actual `NativeSettingsWorker` coordinator
interface is supplied by the host schedule model. No target firmware, physical
SD/RTC, actual Files app, private backend mount, product activation or firmware
delivery is claimed. The original BLE-only case retains the explicitly seeded
namespace-6 baseline. The optional combined case below adds actual Wi-Fi edits.

## Exact application pins

- System public `472036f0c52e8fc88f0ccefb262e25734e6eda39`, tree
  `8fcb0e9c0216d2c16db5115da975e4f5f4132b95`, clean local `758df40` equivalent
- Utilities `fb9da4c531c954e41169509b04debdc30977f387`

The runner verifies the complete System tree and actual transitive controller
headers/source bytes, and records all Runtime, SDK, X4, FatFs and graph hashes.
The copied test bridge calls original controller functions; only its unused
`app_main` symbol is localized to allow section GC. All storage API tables are
returned unchanged by Runtime.

Normal receipts: `evidence/attached-ble-sd-roundtrip/normal`.
Normal observed checkpoints: 19 total, seven deferred admissions while a draft
was live; restore invocation: six total, zero deferred. These are host traces,
not target latency or responsiveness measurements.

ASan/UBSan also passes the attached BLE campaign with the same complete SD
hash `f1632abc17d504c34c1b679bc88d507a557ee93a17eb328a0fbd32a7df1398a7`.
Receipts are under `evidence/attached-ble-sd-roundtrip/asan-ubsan`. LeakSanitizer
is disabled because host process inspection is unavailable; no leak claim is
made. Permanent lifecycle controls intentionally live until process exit.

## Attached two-network extension

Run the same pinned runner with `--include-wifi`. After the BLE sequence, the
unchanged `runtime_atomic_wifi_controller.c` bridge calls the actual final-System
Wi-Fi controller to save `Fixture saved A`, remember `Fixture saved B` only after
its synthetic radio reports connected, and cancel a queued third save. The
original bridge assertions and controller source are unchanged. It holds one
real namespace-6 atomic grant across those edits, then actual controller teardown
releases that grant. Before the controller returns, the real Runtime last-release
checkpoint must leave namespace 6 acknowledged at generation 20 with no pending
groups. The test never explicitly invokes the SD coordinator.

The generation-19 namespace-6 starting map contains only the explicitly synthetic
`setting=six` baseline, no saved networks. A new process after complete native
file/NVS erasure invokes typed pre-app restoration and fresh actual Wi-Fi readers.
They verify both exact SSIDs/passwords, profile count two, saved-page names and
selection of the second profile. The BLE values and both complete native maps
remain checked. The unrelated file and complete SD-image comparison remain
unchanged.

The Wi-Fi bridge's app-local presentation adapter symbols are renamed only in
its compiled fixture object so its actual adapters cannot collide with the BLE
physical/text stubs. `objcopy` commands are recorded in each source manifest;
atomic broker and controller entrypoints are untouched. Wi-Fi radio progress and
UI remain explicit synthetic seams.

Normal combined receipts: `evidence/attached-ble-sd-roundtrip/wifi-normal`.
The complete SD SHA256 is
`74e44725063d9a190ec2d174be5b94c8fc933193e73780ca6386453611660ded`.
The edit invocation observes 22 checkpoints, eight deferred; the fresh restore
invocation observes nine, one deferred. These counts are host traces only.

ASan/UBSan combined receipts: `evidence/attached-ble-sd-roundtrip/wifi-asan-ubsan`.
The sanitizer build passes the identical edit, full-erasure and fresh-reader
campaign with exactly the same source closures, native map hashes and complete
SD hash as the normal build. LeakSanitizer remains disabled as described above.
