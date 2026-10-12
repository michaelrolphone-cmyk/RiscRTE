# Additional actual controller SD roundtrips

This separate test composition preserves the original seven-Settings/Wi-Fi
proof (`NATIVE_SETTINGS_SD_ROUNDTRIP.md`) without changing its sources or pins.

## Qualified increment: Home Quick

`run_native_extra_settings_sd_roundtrip_test.py --controller quick` links the
actual Quick staging/session/action/radio controllers from System
`1e721cc10327691ddf76907f48866a71a2eb02b6`. Namespace 6 remains the independent
Wi-Fi fixture at `a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89`.

The bridge forwards every atomic and legacy-read capability to the actual
Runtime broker. The upstream fixture's atomic and legacy store doubles must
remain unused. Seven user-origin intents exercise brightness 70, brightness
OFF with grouped saved restore level 70, tone 80, DND enabled, Contexts enabled,
Bluetooth and Airplane. Fresh production session/radio decoders verify six
exact restored keys; Runtime/provider KV1/KV2 readers additionally compare
bytes to the restored complete native map.

Both namespaces share one SD image. Normal and interrupted-publication
campaigns use durable checkpoint and typed checked restore after deleting
all native files and simulated NVS witnesses. Restored complete maps and the
whole SD image are compared exactly. The unrelated Files-style file and USB
admission checks are the same as the original test.

Initial empty media/incarnation, hardware sectors, UI/input/radio, SDK NVS and
clock hooks are explicitly synthetic. No target firmware, physical SD/RTC,
actual Files app, product selection or firmware delivery is tested. The
harness alone disables GCC's `aggressive-loop-optimizations` diagnostic for
vendored ArduinoJson's inlined tiny-string path; other warnings are errors.

Normal receipts are in `evidence/extra-settings-sd-roundtrip/quick-normal`.
The source manifest records commands and all transitive source hashes.

## Qualified increment: automatic Home

`--controller home-low-battery` invokes the actual Home low-battery observation
controller. Five boundary-1 automatic intents persist edge, idle/deep timers,
brightness and radio flags. A fresh controller after restoration reads the
persisted edge and does not replay the actions or advance native revision.

`--controller home-rules` invokes the actual rules loop with explicitly
synthetic evaluated rule input. Seven boundary-2 intents apply sleep mode,
idle/deep timers, Wi-Fi/Bluetooth flags, brightness and DND. Rule completion
reports and consume-once behavior remain in the actual controller. No face,
audio or physical radio policy is activated. Actual production preference
decoders verify all restored records.

The upstream rules fixture's standalone `main` symbol is renamed in its object
with objcopy; no controller implementation is rewritten. Both use the frozen
System union pin above. Their alarm helper headers come from Utilities
`fb9da4c531c954e41169509b04debdc30977f387`, pinned byte-for-byte; no Utilities
controller is exercised by these cases. Normal receipts are in separately
named subdirectories of `evidence/extra-settings-sd-roundtrip`.

## Qualified increment: claimed Home preset

`--controller home-preset` seeds one explicitly synthetic, production-encoded
64-byte preset into matching generation-19 native and SD test media. The
actual claimed-room controller performs native read-through lookup, unique
match, synthetic service claim, and actual automatic preference application.
Five boundary-3 intents set sleep mode, 45-second idle, 180-second deep timer,
brightness 60 and DND. A repeated room entry consumes no further intent.
Complete-map comparison includes the unchanged preset, and fresh production
preset/preference decoders verify restored content. The initial seed is not
an app edit or typed restoration shortcut; all post-erasure restoration still
requires the checked SD candidate and native source admission.

All four Home/Quick groups pass normal and ASan/UBSan builds, each with normal
and interrupted-publication campaigns. Sanitizers use `detect_leaks=0` because
this host does not support LeakSanitizer process inspection; leak detection
is not qualified. Receipts and exact source closures remain per-group.

## Qualified increment: Utilities

Four separate choices use exact Utilities
`fb9da4c531c954e41169509b04debdc30977f387` and public System
`472036f0c52e8fc88f0ccefb262e25734e6eda39`, with clean local equivalent
`758df40e72a265ea11cab187ef6410045b8ca26f` required to match whole tree
`8fcb0e9c0216d2c16db5115da975e4f5f4132b95`. Home keeps its separate frozen
union pin. Generated SDK files and all actual C dependencies map back to exact
source bytes. Each case includes the unchanged namespace-6 Wi-Fi fixture.

- `utility-battery`: actual Enable/Disable/Enable controller, three user
  intents and restored broadcast record.
- `utility-ble`: actual alias editor trims a name, creates and clears another;
  failed synthetic HCI enable invokes actual receipt-bound DELETE compensation
  for the previously missing radio record, then successful enable. Six intents
  include a PFR rollback bound to the real operation ID and receipt revision.
  Fresh actual scanner alias and radio readers verify the result.
- `utility-contexts`: actual controller actions enable Contexts and change
  source/timing controls. Three user intents; fresh policy readers verify all
  three records.
- `utility-contexts-automatic`: actual foreground rule controller applies five
  boundary-4 automatic intents in the existing X4 `0x274` order, reports success
  and consumes the evaluation once. Four restored records are decoded exactly.

The shared Utility bridge compares every pending intent byte against the
independently constructed expectation, including native-read prior content,
revision, origin/boundary, desired content, padding and CRC. All storage API
tables come unchanged from Runtime. No Utility fixture atomic/KV double is
compiled. Physical broadcast stop/result, HCI, text entry and detection-service
observations are explicit synthetic boundaries. The actual portable broadcast
adapter's failure latch is covered separately by Utilities qualification, not
by this roundtrip's synthetic result hook.

Unused exported `app_main` (and the Contexts resident descriptor) symbols are
localized in the bridge object so section GC can omit unused renderer/app-entry
paths. The test calls actual controller functions; it does not execute the
complete production UI application. No Runtime storage API or production
controller source is rewritten. The original seven-Settings/Wi-Fi proof also
passes normal and ASan/UBSan on the lifecycle-integrated Runtime source.

All four Utility groups also pass ASan/UBSan, with normal and interrupted
complete-image campaigns. Utility C controllers remain at the upstream-tested
`-O1` under sanitizer; the Runtime/C++ harness remains `-O0`. This avoids the
existing unused Contexts UI format-truncation diagnostics introduced by `-O0`
without suppressing those warnings or modifying production source. No runtime
sanitizer finding occurred. Leak detection remains disabled as described above.

`evidence/extra-settings-sd-roundtrip/qualification-index.json` links nine
controller groups, 18 builds and 36 complete campaigns. It records exact source
cohorts rather than treating distinct System or Runtime pins as interchangeable.
These compositions explicitly call synchronization after controller saves;
none attaches automatic lifecycle maintenance. The dispatcher's live-edit
interleaving qualification is separate.
