# Disconnected native SD attempt model

Increment 1 only. This is not a production Port/graph/provider binding. No SDK,
X4 source, FatFs source, product startup or existing replica consumer was changed.
The published custody design remains the integration authority boundary.

Implemented here:
- Fresh-exclusive admission model; arbitrary warm admission is refused.
- One-use, exact-device Sync credits across the real five X4 async wrappers.
- Exact-scope/raw request/token checks and bounded End cleanup sequencing.
- Truthful public observation: Admission/OwnedSession is distinct from ReadyClosed.
- Checked End plus final RETAINED precedence before outer-fence release.
- One copied native completion job, atomic cancel-versus-commit claim, mandatory
  result collection, and custody pin retention after uncertain/retained results.

The actual composition uses unchanged pinned X4 driver/observer, shared async
adapter, Runtime StorageWorker, Drivers FatFs and the reference sector fixture.
Fresh exclusivity, graph pins, native scope serials, task/ISR admission, lifecycle
bypass gates and core serialization are explicit private test seams. A one-use
credit is never held across a pending result or user callback. NativeSettings
receives no provider-worker/raw-SD authority in the seam.

Completion Evidence is supplied by a trusted native test issuer and the execute
callback is synthetic. These tests do not validate SD content, adopt a native
root, prove a production private issuance capability, or integrate any existing
replica consumer. Candidate validation, core marker/frontier/baseline authority,
source choice and Wi-Fi reconciliation remain separate. The caller must route
cancellation to NativeSdCompletion::cancel while a completion pin is held; the
attempt's ordinary cancel cannot discard that job. Do not call NativeReplicaJob
mailbox cancellation on this path. Never destroy a pending or retained owner.

Qualification in progress: initial composed normal cases passed. Expanded fault
cases, sanitizer runs, strict Xtensa compilation and final memory/source manifest
are pending for this local checkpoint. No physical card/removal, durability,
USB/sleep electrical behavior or total target RAM/stack claim is made.

Runner (all external roots and output are explicit):

    PYTHONDONTWRITEBYTECODE=1 python3 test/run_native_sd_attempt_test.py \
      --x4 X4_ROOT --drivers DRIVERS_ROOT --reference-reader READER_ROOT \
      --output NEW_OUTPUT_DIR [--sanitize undefined|address,undefined]

The runner verifies pinned external source bytes before compiling. Use a fresh
output directory each time. It writes its exact commands and source manifest.
