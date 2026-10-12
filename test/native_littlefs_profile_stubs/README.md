# Host boundaries for the pinned native LittleFS profile

The runner compiles the original files from joltwallet/esp_littlefs commit
`41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317` and LittleFS commit
`f53a0cc961a8acac85f868b431d2f3e58e447ba3`. All eight source/header SHA-256
values are checked before compilation. No vendor file is patched or wrapped to
replace its storage, metadata, file or directory semantics.

These authored host declaration shims expose only the ESP-IDF, FreeRTOS,
newlib and physical-flash boundaries required by those sources. In particular,
`esp_vfs_register` captures the actual table and context created by the vendor;
inspection calls invoke those function pointers. No host POSIX filesystem is
used for simulated app-data storage. The concrete newlib DIR prefix, glibc
`struct stat`, callback table and partition declarations are host boundary
models, not evidence that a target binary ABI has been reproduced. The vendor
stat implementation itself supplies the zero inode/device/link-count/ctime
fields. Seconds-based mtime is enabled.

The fixture uses a bounded 512 KiB array at the modeled appdata address
`0x270000`. The original `littlefs_esp_part.c` routes LittleFS operations to
checked partition reads, one-to-zero programming, and 4096-byte aligned erases.
Every operation is counted. Read/program/erase byte totals and rejected bounds
or illegal programming attempts are separately visible. The flash array can be
snapshotted and restored; restore remounts with formatting disabled and advances
the registration generation. Only explicit test reset formats an empty model.
A reset creates `/private` and `/private/n00000006` through the real VFS.

Faults trigger once at the requested operation count, either before execution
or after the underlying operation has run. Short reads make a genuinely smaller
VFS read request. Close-before faults preserve a live handle; close-after faults
consume the real handle while reporting failure to exercise conservative
retention. The consumer must never retry an uncertain close.

Test-owner shutdown/reset/restore model loss of volatile RAM between cases,
not successful runtime close or quiescence. They do not call file close, flush,
sync or retry; open file cache buffers and directory allocations are discarded,
then actual VFS unregistration releases shared memory. This teardown alone
uses the pinned private directory layout to avoid leaking host allocations.
No inspection result or on-flash bytes are changed by this cleanup. The
`reboot` helper performs this discard and remounts the same flash without
formatting. `restore` discards then replaces flash from the supplied snapshot.

In this pinned stack, a low-level `lfs_file_close` consumes its handle even
when sync fails, while the ESP VFS retains its descriptor. The fixture preserves
that uncertain logical handle for the consumer; test-owner teardown checks
actual LittleFS attachment before releasing a cache, avoiding double free or
close retry. This does not repair vendor behavior or establish safe recovery.
Hook callbacks run before and after operations; recursively
invoked callbacks are suppressed, but nested operations are still counted.
Mutation helpers reject invocation from inside active fixture operations.

FreeRTOS mutex calls use host pthread recursive mutexes. Single-host execution,
including the actual native atomic lease used by the consumer, is not an RTOS,
flash power-loss, target ABI, mounting-policy or hardware qualification.

The runner builds authored C and C++ with strict warnings. `SANITIZE=1` enables
AddressSanitizer and UndefinedBehaviorSanitizer. Under a ptraced executor,
LeakSanitizer cannot run; explicitly use `ASAN_OPTIONS=detect_leaks=0` in that
environment and report leak checking as unavailable. The runner does not
silently suppress sanitizers. `SANITIZERS=undefined` selects UBSan alone.

A separate negative probe found that failing the second flash program before
creating/writing an 8192-byte new file can make this exact pinned stack assert
in `lfs_bd_prog` (`pcache->block == ((lfs_block_t)-1)`). The same second-program
fault with a 508-byte write did not assert. Arbitrary large-write program-failure
recovery is therefore not qualified by successful read-only profile tests or
508-byte prefix composition. Vendor assertions and vendor sources remain enabled
and unchanged.

## Separate replayable probes

From the repository root, optional offline source locations can be supplied:

```sh
export ESP_LITTLEFS_SOURCE=/path/to/pinned/esp_littlefs/checkout-or-flat-files
export LITTLEFS_SOURCE=/path/to/pinned/littlefs
```

With neither environment variable set, the runner fetches the exact pinned
files and still verifies every hash. The normal success suite is:

```sh
python3 test/run_native_littlefs_profile_test.py
```

The short RAM-discard smoke test is separate and must pass:

```sh
python3 test/run_native_littlefs_profile_test.py \
  --test-source test/native_littlefs_reboot_test.cpp
```

Under a ptraced executor, its ASan+UBSan replay is:

```sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 \
  python3 test/run_native_littlefs_profile_test.py \
  --test-source test/native_littlefs_reboot_test.cpp
```

The deterministic large-write negative probe is also separate:

```sh
python3 test/run_native_littlefs_profile_test.py \
  --test-source test/native_littlefs_large_write_fault_probe.cpp
```

Expected negative outcome: actual VFS open returns descriptor 0, then the
8192-byte write aborts at `littlefs/lfs.c:262`, `lfs_bd_prog`, with assertion
`pcache->block == ((lfs_block_t)-1)`. The test process receives SIGABRT; the Python
runner exits nonzero (1 on the recorded host) after reporting that signal. This
failure is evidence of an unqualified mutation path, not a successful recovery
test. It must never be added to a success suite as though the write succeeded.
Short replay logs are stored under
`experimental/native_replica/evidence/littlefs-profile/`.

An exploratory 508-byte smoke swept 120 injection attempts (three flash
operations, before/after, cuts 1–20). Some late cuts were not reached; the attempt
count is not a count of triggered failures and is not used as coverage evidence.
