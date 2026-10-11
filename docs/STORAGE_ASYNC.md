# Copied asynchronous storage (development Runtime 0.2.7)

The installation feature selects `platform.storage-worker@1` on the SD hardware
device. Native code reserves that device's exact power/CLK/CMD/DAT0 pads before
binding provider code. The worker receives a scoped storage I/O table; ordinary
Runtime, GPIO, clock and synchronization APIs do not grant it owner authority.
Storage work never sets the global peripheral transfer fence.

`RiscStorageWorkerV1` owns one copied request and result (4608 bytes each, at
least 8-byte aligned). Queued cancellation prevents execution. Running cancellation
stays pending until the callback returns. Failed cleanup retains code and grants;
there is no timeout-based task deletion or unload. Buffers/control atomics are
allocated in aligned internal RAM only for an explicitly selected dependency.
The persistent worker uses an 8 KiB internal stack; measure its high-water on
hardware before release.

`RiscStorageVolumeAsyncV1` is a tagged suffix to the existing filesystem table.
It exposes a copied-request session for refresh, directory enumeration and small
file reads. The reusable provider adapter owns its directory and temporary file
handles. Ending a session cancels outstanding work, then queues checked cleanup;
owner polls never cancel a queued cleanup repeatedly. A failed close is terminal
retention. Drivers reject synchronous admission/export while a session is live.

`InstalledAppAsyncDiscovery` performs no synchronous volume calls. It coordinates
submitted operations, stages complete manifest strings, and closes its session
before publishing. Invalid individual manifests can be skipped; failed media or
incomplete directory enumeration cannot remove previously published catalog rows.
No executable read/hash, app-identity list, panel state or input gate is present.

Validation includes the delayed-callback mailbox tests, cancellation/retention
adapter tests and scoped CPU tests. X4's production SD driver/shared FatFs tests
cover lazy activation, 1000 pending owner polls, new manifest reads, refusal of
USB export during a scan, eight-sector export batching after cleanup, cancellation,
exit and sleep/resume. UBSan caught an alignment mismatch in the C worker fixture;
both native and fixture buffers now have an explicit 8-byte contract and sanitizer
runs fail immediately on any report.

This is source-level and host/target-build validation. No hardware execution,
flash, final UI acceptance or release is implied. Cached manifest persistence
still uses the existing internal app-data atomic-replace backend; making those
writes asynchronous is a separate remaining integration step.
