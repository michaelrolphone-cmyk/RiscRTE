# Consumed request compaction qualification

The production protocol uses a separate terminal string, permanent READY/ACTIVE
scalar, and boot-lifetime native NVS/PHY quarantine after an ambiguous mutation.
Source commit `f7df4ee2ce8d718d48b124bf011f6e07d5f66ce3` was published with
exhaustive qualification pending. **Both unchanged frozen matrices subsequently
passed**, as bound by `completed-qualification.json` and exact tool records:

- Target NVS configuration: **22,762,489 assertions / 7 cases**.
- Strengthened NVS assertions: **22,750,891 assertions / 5 cases**.
- Each: **114,074 injected flash boundaries** (83,298 replacement, 20 legacy
  READY initialization, 30,756 first enqueue). Combined: **228,148 boundaries**.
- Assertion-enabled real-NVS ASan/UBSan repeat/capacity/CRC: **11,920 assertions /
  4 cases**.
- Normal and ASan/UBSan core/bridge/global-NVS/native-PHY tests pass; actual
  ESP32-S3 queue/bridge and NativeHardware compilation passes.

Final logs are `nvs-target-complete.log` and `nvs-assert-complete.log`. The earlier
`pending-frozen-*.log` snapshots and their timestamp remain unchanged historical
evidence of publication status. No source or running test inputs changed during
the frozen runs. This is host/SDK qualification, not physical-device power-loss
qualification or qualification of later source changes.

## Rejected designs and preserved findings

- `rejected-same-key-pressure.log`: IDF 4.4.7 failed multipart replacement
  damaged the old receipt at alternate generation under unrelated-key pressure.
  Original 24 KiB dumps remain in the standalone qualification workspace;
  `rejected-flash-sha256.txt` binds them.
- `rejected-string-crc.log` and `independent-string-crc*`: without the scalar,
  IDF's string CRC read erased a damaged terminal, so later reads and reboot
  incorrectly selected Missing. Those source hashes identify the rejected
  pre-marker snapshot.
- `rejected-wrong-type-marker.log`: typed getters alone misreported existing
  wrong-type records as Missing. The corrected metadata adapter independently
  passes all 30 type combinations plus iterator-allocation failure and CRC
  fencing: **748 assertions / 5 cases**, in `independent-type-matrix*`.
- `rejected-live-sdk-reconciliation.log`: immediate queue read after a returned
  mutation error hit IDF's strengthened host assertion. The target configuration
  returns an error instead, but this is not a safe live-cache reconciliation.
- `rejected-queue-only-quarantine-{target,assert}.log` and
  `rejected-unrelated-live-cache.log`: quarantine confined to the queue still
  permitted unrelated live-cache getters to report false Missing. Untouched
  raw-flash reboot recovered the exact keys. The final shared latch fences all
  native NVS clients and new PHY starts until ordinary boot. The raw snapshot
  is bound by `rejected-unrelated-flash-sha256.txt`.

The upstream source is pinned to ESP-IDF 4.4.7 commit
[`38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3).
Relevant primary implementations are
[`nvs_storage.cpp`](https://github.com/espressif/esp-idf/blob/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3/components/nvs_flash/src/nvs_storage.cpp),
[`nvs_page.cpp`](https://github.com/espressif/esp-idf/blob/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3/components/nvs_flash/src/nvs_page.cpp),
[`nvs_api.cpp`](https://github.com/espressif/esp-idf/blob/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3/components/nvs_flash/src/nvs_api.cpp), and
[`phy_init.c`](https://github.com/espressif/esp-idf/blob/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3/components/esp_phy/src/phy_init.c).

## Fixture provenance and limits

The fixtures are the exact public profile/time objects from the unpublished
2026-10-14 product qualification feeds. They contain no Wi-Fi credentials or
candidate binaries; `example.invalid` URLs are inert test inputs. The tests
supply only synthetic Wi-Fi, including maximum escaped 32-byte SSID / 63-byte
password values. They do not download or admit assets.

- Watch: 125 files, public profile 12159 bytes; original full-feed SHA256
  `1dc4168be4c0123908dce5b9bcd52670aaf6145dbba53d23ba4f8d4d6f0e4d6b`
- Wave: 132 files, public profile 12883 bytes; original full-feed SHA256
  `c7a08524af2dd0cb1c4e5556a919ce03241a36e7d010b7d7c0806cac500315ea`
- Explicit time descriptor is the caller-selected `time.cloudflare.com`.

The real engine uses six 4096-byte pages. Unrelated data includes a string,
768-byte blob and up to 200 requested u32 pressure keys (only successfully
written keys count). Ordinary and maximum-escaped actual profiles, 12 successive
changed cohorts, exact replay, both blob histories, and the 16928-byte maximum
record are exercised. Finite capacity can still refuse a request; no partition
resize or unrelated-data deletion is permitted. Every ambiguous mutation holds
UNKNOWN and quarantines the shared backend until ordinary boot.

Each real-NVS runner creates a clean build directory and prints exact source
hashes and an explicit NVS assertion configuration. No stale object/header or
cross-variant cache is reused. Its process-local request guard composes the
production queue with the actual global latch and native KV/settings/bootstrap/
owner-install adapters; the native bridge test separately proves status, queue
and boot-input calls make no NVS opens after UNKNOWN. Successful publication of
a new request requires READY, absent terminal and exact pending bytes both
immediately and after reboot. Identical consumed-request replay deliberately
keeps its terminal and does not publish a pending request.

## Reproduction

```sh
bash test/run_request_queue_compaction_test.sh
bash test/run_native_provision_request_test.sh
bash test/run_request_queue_test.sh
bash test/native_nvs_shim/run_test.sh
bash test/run_nvs_quarantine_radio_test.sh
# Repeat the above with ASAN_OPTIONS=detect_leaks=0 SANITIZE=1.
python3 test/compile_native_provision_request_target.py --packages "$PLATFORMIO_PACKAGES" --output /tmp/request-compaction-target
# ESP_IDF is the pristine pinned tree with its pinned mbedTLS built.
ESP_IDF="$IDF_447" bash test/run_request_queue_nvs_test.sh
ESP_IDF="$IDF_447" NVS_ASSERTIONS=1 bash test/run_request_queue_nvs_test.sh '[cuts],[scalar-init],[crc],[first]'
ESP_IDF="$IDF_447" NVS_ASSERTIONS=1 SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_request_queue_nvs_test.sh '[repeat],[capacity],[crc]'
```

The broader IQ port runner fails an unchanged `gpioDeepSleepHold` assertion
before native IQ tests. This was reproduced on clean published base
`0f42274ba8d091712a1f875f18851a6f3a996a66`; exact command/results are preserved in
`published-base-iq-port-failure.json`. It was not modified by this checkpoint.
The focused native PHY gate tests pass independently, normal and sanitized,
including all five existing IQ ROM/layout variants and active-session cleanup.

The finite-entry operation shim is a conservative lower-bound model, not the
SDK GC engine. It covers all 23 replacement-operation boundaries. Real-engine
cuts interrupt each 4-byte write or complete sector erase. Abrupt cuts reboot
untouched raw flash before selection; returned errors first prove global
quarantine without SDK reads, then recover on ordinary boot. Neither model
qualifies partially erased physical sectors, arbitrary multi-key destruction,
physical hardware power loss, whole-task stack/RAM, TLS or flash lifetime.
LeakSanitizer is disabled under ptrace; ASan and UBSan remain active.
