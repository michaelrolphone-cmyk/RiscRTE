# Scoped user-volume adapter

This is an unbound storage prerequisite for a shared File Browser or file-transfer
application. It does not expose a server, mount storage, alter boot grants, add a
capability alias, change a product/native pin, or make Watch/X4 writable today.
An isolated [provider/root binding](SCOPED_USER_VOLUME_BINDING.md) now composes
this adapter with an exact existing ProviderGraph grant; product/app authority
and native user-storage selection remain separate.

`RiscStorage::ScopedUserVolume` wraps an **already admitted** ordinary
`storage.volume@1` provider. `begin()` still publishes the exact existing v1 prefix.
The separate `beginExtended()` entry explicitly selects the canonical optional
extension's checked directory close, local handle errors, mkdir and rename.
The upstream must implement the canonical prefix-compatible extension, including
checked directory close, file information/sync, handle errors, and exclusive
rename. The runtime does not implement a second filesystem or hardware transport.

## Source custody

- Base is the byte-exact delivered Runtime 0.1.99 archive tree
  `fc135aef0b12314df77a793ab6f94ca95b84ed02`, recorded source revision
  `11b16cf60c5d3bd9b6a202f25b7b86a2ff05ca21`. The local archive-import commit is
  only a custody checkpoint; it is not a substitute public source revision.
- Runtime version is unchanged. This unit is not a rebuilt/released firmware.
- `sdk/app/RiscStorageVolumeV1.h` is copied exactly from Reader
  [`3f59913963840cb5ee8e2a65baa921f8d6af0b4d`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3f59913963840cb5ee8e2a65baa921f8d6af0b4d/sdk/driver/RiscStorageVolumeV1.h),
  blob `800bc4c7feeb42595091627b707cc7fac745ff3d`, SHA-256
  `d477167a06004640b2c07c05d662c103be6e1b9e4a4d866f6fce13e754ef02f5`.
  The original v1 layout and semantics are unchanged; existing upstream extension
  declarations are restored, not invented or reinterpreted.
- The recovered .99 archive's separate resident-loading SDK omissions are not
  repaired by this unit. No .98/.99 concurrent source is overwritten.

## Admission and confinement

The trusted owner calls `configure(upstream, "/User", "User files")`, then
`begin(&table)` after the provider graph and explicit storage root are admitted.
Configuration performs no provider I/O. Begin checks ready state and the existing
root directory; it never creates, formats, discovers or guesses a root.

The selected root must contain only the shared user files the application is
allowed to manage. It must not include installed firmware, private app-data,
credentials or provisioning state. Those existing namespaces are unchanged.
There is no implicit grant based on an application/package name.

`beginExtended()` additionally requires an upstream mkdir callback. Missing
support fails before provider I/O and leaves the output untouched; ordinary
`begin()` remains available. The SDK header, API version and original v1 table
layout are unchanged. The extended table's file_open, file_seek, file_info,
file_sync and dir_rewind callbacks are null; consumers must test the callbacks
they need rather than infer complete filesystem support from struct_size.
Optional-callback availability is copied at configuration; re-admission never
dereferences a previously borrowed provider table before fresh custody checks.

All callbacks accept normalized absolute **virtual** paths such as `/Books/a.txt`;
these map only to `/User/Books/a.txt`. Relative paths, empty components, traversal,
trailing dot/space aliases, drive syntax, controls and FAT-reserved punctuation
are refused. Names are at most 127 bytes, virtual/configured roots at most 192
bytes. A root equal to `/` is refused. The admitted upstream must not implement
symlink, reparse, hard-link or equivalent aliases that can cross the selected
subtree. The existing FatFs provider has no such links. This adapter is not a
security wrapper for an arbitrary untrusted path backend.

Two non-mutating hooks are required: the current owner-executor predicate and a
provider-custody predicate. Every instance must use the same serialized executor;
the registry is not a cross-thread filesystem lock. A false owner predicate rejects
without provider calls. Lost custody permanently retains the adapter. The custody
hook must detect upstream invalidation/generation loss and retained native work.
It is checked before and after operations and between chained upstream callbacks.

## Bounds and lifetimes

- Up to four adapter instances; each session owns at most one reader, one
  exclusive staged writer and one directory. The reader and writer have separate
  local/upstream handles, failure flags, sizes and offsets. This adds no heap
  allocation, grant slots, implicit volume access or upstream capacity: a provider
  that cannot supply both file handles refuses the second open normally.
- Reads/writes copy at most 512 bytes per callback. Read data is published only
  after success and the final custody check. Writes snapshot that bounded chunk.
- Directory iteration streams without an inventory-size cutoff. A callback skips
  at most eight dot/internal entries. Eight consecutive skipped entries exhaust
  that callback and report an explicit error without probing a ninth, not EOF.
- Contexts and file/directory handles use process-wide non-reusing counters;
  exhaustion fails closed. Saved tables/handles cannot revive after end/reacquire,
  another instance, or reuse of the same object address.
- Refresh and remove reject while handles are open. Read size/position is checked
  against upstream file information. This does not promise a content snapshot
  against concurrent external, same-size in-place writes.
- `dir_next == false` plus an empty local `last_error` means EOF. Checked-close
  failure is observable through `last_error`, `retained()` and `end()` even though
  the legacy directory-close callback returns void.
- The opt-in `dir_close_checked` returns the same checked outcome directly.
  Its `handle_error` is a local, non-clearing diagnostic: zero for a live,
  healthy matching file/directory handle and nonzero for failed, stale,
  wrong-kind, foreign, retained or wrong-owner access. It never calls upstream
  or exposes upstream tokens. A failed directory iteration remains an error,
  rather than an empty directory, even when unrelated diagnostics are read.

## Writes and uncertainty

Exclusive creation stages in the destination's existing directory. A reserved,
uppercase 8.3 name `~Rxxxxxx.TMP` avoids creation of a second FAT short-name alias.
Every case variant of the `~R` component prefix is reserved and inaccessible through
this adapter, and staged entries are omitted from enumeration. Stage counters do
not reuse names in a process; an old stage collision is refused without deletion.

The destination stays absent until commit. Commit syncs and checks close, then
uses the upstream's existing **exclusive rename**. It never unlinks or replaces
a destination. A destination that appears during the transaction is preserved.
Short writes, write errors and sync failure prevent publication; a successfully
checked `close(false)` rolls back the unfinished stage. End also rolls back any
live writer and closes the directory before retiring the session. End closes an
existing reader first, then aborts the writer and closes the directory, stopping
at the first unconfirmed close without touching remaining owned resources.

A checked reader close reports successful cleanup even after a prior read error;
the failed read was already reported and closing a reader publishes nothing.
This allows the real browser copy path to abort its destination and finish cleanly
after a read/write/size-check failure. By contrast, if a caller requests commit
after a writer failure, including a sync failure found during commit, publication
has failed. The v1 bool result must be false. Even if rollback was checked and
completed, the adapter then retains locally so a consumer's unconfirmed writer
cannot coexist with an apparently unloadable session. It never retries provider
cleanup or converts a later close into a false publication-success report.

Any uncertain file close, rollback, directory close or publication latches terminal
retention. Even a failure reported after the side effect must not trigger a second
close, delete, rename, remount or provider call. Readiness, new I/O and end fail;
only the copied local diagnostic remains readable. The owner must retain the
adapter, provider, dependencies and mapped code. Destruction is not recovery and
performs no upstream cleanup. The caller must check `end()` before unloading.

This supplies transactional visibility/rollback within the admitted live session.
It does **not** claim power-loss atomicity of FAT directory metadata. Interrupted
or uncertain publication can leave a hidden stage, a committed destination, or
media requiring recovery. On a later boot, pre-existing stages are preserved and
hidden; no automatic garbage collection or destructive repair is implemented.

## Opt-in folder creation and rename/move

`beginExtended()` supports creating one directory and exclusively renaming a file
or directory, including moving a directory with its existing subtree. It does not
create parent directories or recursively copy data. Both source and destination
are validated and copied before provider I/O; they remain under the same admitted
root. Root mutation, reserved stages, traversal, invalid names and overlong paths
are rejected before any provider call. Any live file or directory handle blocks
these mutations until checked cleanup succeeds.

A known existing destination is refused without calling the mutation callback.
The provider remains authoritative for alias-aware collision handling and any
destination appearing after that check. Exact/ASCII-case source-equal or descendant
destinations are rejected early; the admitted provider must also reject moves
into a subtree through filesystem-specific aliases. The canonical Reader FatFs
provider already performs directory-cluster checks for this purpose. Case-only
renames are deliberately unavailable. Existing destinations are never deleted or
overwritten, including a competing writer's destination.

The bool upstream mutation result cannot distinguish failure before a side
effect from failure after it. Consequently a failed dispatched mkdir or rename
latches terminal retention, even if the underlying failure might be benign.
There is no automatic retry, inverse rename, deletion, remount or success claim.
Owner/custody checks also fence every chained callback and suppress success after
custody is lost. Keep the adapter, provider and dependencies alive until recovery.
This is a live-session contract, not power-loss-atomic rename or a recoverable
Trash feature.

## Deliberate remaining work

The original v1 ABI layout remains unchanged; the explicit extended output adds
mkdir and rename/move. Neither provides seek, overwrite/replace, quota management
or recoverable Trash. Bounded same-volume copy now works through the unchanged
shared browser with its source reader and exclusive staged destination writer.
This is not complete file-manager management or a WebDAV implementation.
Product provisioning/root selection, a suitable Watch user-storage provider,
boot/provider admission, Wi-Fi/WebDAV, BLE discovery/brokering and UI integration
remain separate work. Installed app images and private app-data are not exposed.

## Reproduction

From this repository:

```sh
bash test/run_scoped_user_volume_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_scoped_user_volume_test.sh
bash test/run_scoped_user_volume_fatfs_test.sh /path/to/verified/T5S3-Reader
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 \
  bash test/run_scoped_user_volume_fatfs_test.sh /path/to/verified/T5S3-Reader
bash test/run_installed_files_test.sh
bash test/run_app_data_files_test.sh
bash test/run_app_data_runtime_test.sh

# Unchanged shared browser copy path, with the adapter's fault model:
bash test/run_scoped_user_volume_browser_test.sh /path/to/verified/System-Apps
# Actual shared browser + scoped adapter + production FatFs on a RAM disk:
bash test/run_scoped_user_volume_browser_fatfs_test.sh \
  /path/to/verified/System-Apps /path/to/verified/T5S3-Reader
# Both scripts also support SANITIZE=1 and ASAN_OPTIONS=detect_leaks=0.
```

The second harness compiles the actual unchanged Reader `volume.c`, `ff.c` and
`ffunicode.c` against a RAM disk, verifies canonical-header equality, exercises
staging/aliases/isolation/commit/rollback, folder creation, recursive directory
move and file rename. It injects failures at every observed sector-write boundary
in stage creation through publication (7), mkdir (6) and rename (1). These are
operation errors in a live RAM filesystem, not simulated reboot durability.
No transport, GPIO,
host-mounted filesystem or physical device is used. The local reference checkout
is the exact Reader revision above; no dependency is downloaded by the scripts.

The deterministic adapter suite has 28 groups, including safety revocation after
each of 18 provider callback kinds, reentrancy, copied-input snapshots,
cross-instance/stale-context refusal, malformed directory entries, reserved-entry
skip branches, and failure before/after close, rollback and rename side effects.
Six extension groups cover opt-in/legacy ABI behavior, absent support, stale
contexts/handles, directory subtrees, known/racing collisions, invalid paths,
busy handles, diagnostic preservation, failure before/after side effects,
checked close, every new callback fence, reentrancy and copied inputs.
Four dual-handle groups cover both open orders, strict one-reader/one-writer
bounds, error-state independence, checked ordered cleanup, stale tokens and
custody loss with both resources live. The browser composition compiles the
unchanged real `fbx_copy`, controller, adapter and host fixture from System Apps
`1605858` with its own SDK and checks the volume ABI layout against this Runtime.
Seventeen isolated browser cases cover success, empty files, collisions,
unavailable source/writer, read/write/size failures and uncertain cleanup or
publication. A further composition uses that same browser and actual unchanged
Reader FatFs, verifies both source and destination bytes, and injects all 16
observed sector-write failures during a 5000-byte same-volume copy. These are
component host tests; they do not install the browser or bind a Runtime grant.
Installed-file and app-data model/fault/actual-ELF regression suites pass normally
and with ASan/UBSan. These are local checks, not hosted CI.

The preceding v1 checkpoint (`a08d795`) also compiled with pinned Xtensa ESP32-S3
GCC 8.4 (`-std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti`). The extended
operations delta is host-tested only; no new target build, firmware link or
hardware qualification is claimed.
Local sanitizer runs retain AddressSanitizer and UndefinedBehaviorSanitizer;
LeakSanitizer is disabled for the executor's ptrace restriction.
