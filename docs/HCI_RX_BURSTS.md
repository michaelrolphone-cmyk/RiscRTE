# HCI receive burst repair, Runtime 0.1.73

## Reproduced defect

Runtime 0.1.72 gives every received HCI packet a 1,028-byte payload slot and
provides only four slots. Four short advertising reports leave most allocated
bytes unused, but the fifth faults the entire stream. A command-complete event
queued ahead of the reports consumes another slot. This can make the passive
scanner fail during the transition from controller startup to scanning. It is
independent of whether the provider was just activated or retained from an
earlier app.

The selected X4 scanner is cooperative: it polls at most six packets per call,
and display/input work occurs between calls. A burst can therefore arrive before
the next drain. The software reproducer injects 32 distinct valid advertisements
with the LE scan-enable acknowledgement and, separately, between polls. It does
not assume a hardware event rate or claim to explain every reported startup
failure.

## Repair and bounds

The native port stores copied packets in a 4,124-byte ring. Each record contains
one type byte, a two-byte payload length, and exactly that payload. This fits four
maximum 1,028-byte ACL packets, 242 fourteen-byte advertising events, or a mixed
FIFO sequence within the same bound. TX remains a separate 1,029-byte copied
buffer. The combined allocation is 5,153 bytes, no greater than the prior 5,158
bytes. Queue state has no per-packet allocation.

All writes, reads and wiping run under the existing native lock. A copy spans at
most two ring segments. There are no app/provider pointers, silent drops,
priority reordering, changed SDK calls, retries, or ABI changes. Malformed
packets and true exhaustion still fault the session. A failed controller close
retains the exact session and allocation; successful close wipes everything.

## Reproduction

Run from Runtime with local repositories containing these exact pinned objects:

```
python3 test/run_hci_scanner_burst_test.py \
  --watch /path/to/RiscRTE-T-Watch-S3 \
  --drivers /path/to/RiscRTE-Drivers \
  --output build/ble-scanner-before \
  --native-ref 0517db42c1df40cc88d8f9385435d44972e7e5d9
```

The prior native source fails the real scanner's startup poll. Omit
`--native-ref` to exercise the repair. Add `SANITIZE=1` for ASan/UBSan.
The fixture extracts, hashes and compiles unmodified sources from:

- HCI provider: `4966acee548e6cc3997289db206e996ed4c15cda`,
  `drivers/twatch_ble/driver.c`
- Sensor scanner: `aa5ce0140102bf4d1039e5f208588a6793d06b28`,
  `Drivers/ble_sensors/driver.c` and its production `ble_scan_core.h`

The fixture connects both real providers to actual CpuPort and NativeHci, with
SDK functions replaced by deterministic controller status, command replies,
advertisement arrivals and cleanup failures. It runs 20 open/close cycles with
the same provider mappings, alternating prior controller OFF/ON, preserves all
640 report identities and verifies failed-close retention before retry. It does
not substitute a toy scanner state machine. It also accepts 242 short reports,
faults on report 243 at the real byte limit, and proves cleanup retention/retry
through the same providers. Provider sources stay external and their versions
are unchanged.

`bash test/run_hci_test.sh` additionally covers 32 short packets, 600 mixed-size
enqueue/drain operations across wraparound, four maximum ACLs, malformed lengths,
both split-header positions, true capacity exhaustion, wiping and stale callbacks. Existing graph/dlopen
tests cover invocation lifetime, default-child-default navigation, owner/token
checks and failed-open/close retention. Stage-on/off native tests remain in
`test/run_radio_stage_test.sh`.

## Wi-Fi and state distinctions

This change affects Bluetooth reception. It does not establish the cause of the
reported Wi-Fi intermittence. The inspected X4 Wi-Fi 0.2.0 provider is unchanged
from Watch source `cf30d732271db19a74492d4753736cafeafb92bb`; its native Runtime
adapter is unchanged between the delivered 0.1.69 and the 0.1.72 baseline.
The existing production native station/scan fault suite passes setup failures,
stale event isolation, connection timeout, cancellation, failed cleanup and
explicit retry after successful cleanup. IQ ownership guards also pass. These
checks rule out the exercised software transitions, not RF/PHY or power faults.

In the selected product, Wi-Fi ON means an allowed policy; connection and scanning
are explicit actions. Bluetooth ON means controller-ready, not scanning,
advertising, paired or connected. Default preferences allow Wi-Fi and leave
Bluetooth off. Provider acquisition itself does not start either radio. The
selected demand-retained lifetime preserves idle provider mappings across app
navigation; cold reset creates new native state.

A remaining measurable Wi-Fi hypothesis is transient native allocation or SDK
setup failure under the complete retained product's heap footprint. The existing
numeric stage result identifies which SDK step rejected the operation. A useful
next software experiment must reproduce that exact allocation state with the
full selected provider graph; arbitrary retries or changes to policy are not a
demonstrated repair. ESP-IDF's early controller-init failure is still treated as
uncertain, because public IDLE status cannot prove PHY/power rollback.

The exact Wi-Fi application source at
`7b175418e3063d9f4c571acf06c366144831b2af` also passes its 44 startup/policy cases
across plain/stage and normal/ASan/UBSan configurations, plus 48 production
controller cases in both normal and sanitized stage builds. The existing
`test_radio_startup.py` runner omits `-DTEST_IDLE_ELIGIBILITY` for that final
controller binary and fails to link; the 48-case runs used the required existing
macro directly. This test-runner defect is separate from device radio startup.

No hardware, RF reliability, pairing, physical power or coexistence result is
claimed. Target compilation proves SDK compatibility and linking only. No build
is delivered or published by this repair.
