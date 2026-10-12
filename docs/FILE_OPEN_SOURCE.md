# Runtime-owned file sources (version 1)

`file.open.source@1` is additive. `file.open@1`, its canonical header and all
storage API layouts are unchanged. Both source caller and selected receiver
need an explicit admitted requirement and grant. `Port::fileSource` defaults to
null; a boot policy requiring the capability without a complete trusted backend
fails admission. This does not expose a filesystem path or a new volume.

## App contract

- Discover the existing admitted `supported_file_types` handlers with a basename
  such as `mesh.OBJ`. Names are bounded valid UTF-8, not absolute/relative paths.
  Handler enumeration includes older handlers; `handler_supported(app_id)` says
  whether a handler also has the exact source capability.
- `begin(name,size,&token)` creates one private sequential capture. Submit at
  most 4096 bytes to `write` and advance by the successful actual count, which
  may be shorter. BUSY starts no operation. A declared size is a checked extent,
  not a RAM allocation. Only the original live caller owns staging authority.
- `finish(token)` seals exactly the declared size. Incomplete capture cannot be
  opened. `open_request(token,app_id,cookie)` accepts only an admitted compatible
  receiver that has the source grant and is allowed by the resident policy.
- After acceptance, close caller inputs, release grants and return. Runtime owns
  the private disk source across unloads and resident/legacy continuations.
  No app callback, remote connection or borrowed pointer survives the caller.
- The selected receiver obtains `source_info` and uses seekable `read_at`, at
  most 4096 bytes per call. Advance by `actual`; zero at the end is EOF. The
  source is read-only. Wrong contexts/generations cannot read, stage or discard.
- Receiver return or clean load/init failure removes the private source and
  reloads the caller with `take_result(error,cookie)`. Results indicate receiver
  invocation success/failure, not whether parsing succeeded. Source results are
  consumed with the source API, never `file.open.open_take_result`.
- Explicit Home removes the source and abandons its continuation. Interrupted
  caller capture is removed after successful caller teardown. Cancel removes
  the private source only. Non-BUSY capture errors remove the stage; same-live-
  caller `discard` acknowledges that cleanup idempotently without retrying I/O.
  Fresh begin/unload/handoff revokes that acknowledgement.

All bytes stay private until a successful checked read. Failed close or native
custody is terminal: Runtime, descriptor, mount, app/provider images and native
leases are retained. No automatic close/unlink/recovery retry follows. A cleanup
failure at a lifecycle boundary also prevents any fresh launch.

## Trusted backend and native selection

`FileSourceBackend` is a native-owned callback contract, not an app interface.
The disk adapter stores a copied private path and extent metadata. Each bounded
operation checks its descriptor close; there is no complete-file RAM buffer.
A port may supply a free-space callback/headroom and a smaller per-call I/O bound.
Only a cold configuration can remove the exact stale `.risc-file-source` file;
there is no directory scan, recursive deletion or original-file mutation.

The optional ESP32 binding uses only `/appdata/.risc-file-source` on the existing
NativeAppData mount, with `RISC_NATIVE_FILE_SOURCE=1`, paired banks/app-data and
`RISC_NATIVE_APP_DATA_MOUNT=1`. It does not mount, format, resize, select an
inactive firmware bank or change namespace mapping. Its shared checked mount
fence is a separate required integration; ordinary builds remain off.

The source binding takes the existing native radio lease and the checked real-
mount guard for each complete operation. It pins both after terminal errors.
It requests 512-byte native I/O chunks and reserves 64 KiB of current app-data
free space. That reserve is a policy margin, not a guarantee that another
settings transaction will fit. Current native app-data is 512 KiB total, so
larger files return NO_SPACE. Remote range streaming is not implemented; host
12 MiB tests establish bounded streaming, not Watch storage capacity.

The pinned LittleFS VFS has a reproduced false-success open after internal sync
failure. Native calls therefore require the shared global fault fence and use
conservative errno/error checks. Short writes alone do not qualify that VFS.
Host injected-fault tests are not physical flash/power-loss qualification.

## Verification

- `bash test/run_file_source_storage_test.sh`: 12 MiB sequential capture and
  random seeks, full disk, headroom, cancel/interruption, short I/O, original-file
  preservation, symlink refusal, read failure privacy, failed close, false-
  success open and terminal no-retry behavior.
- `bash test/run_file_source_runtime_test.sh`: real bootstrap/Runtime and loaded
  app fixtures, explicit admission, immutable copied basename, unsupported
  handler, stale generations, caller/receiver cleanup, reopen, receiver load/
  init failure, Home, ordinary and resident/legacy chains, retained failures.
- Both accept `SANITIZE=1` for AddressSanitizer and UndefinedBehaviorSanitizer.
  This executor requires `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer
  cannot run under its ptrace environment. That is not a leak-test pass.
- Existing `test/run_file_open_test.sh` and `test/run_resident_legacy_test.sh`
  continue to cover the unchanged older handoff and resident behavior.

Additional integration checks:

- `FILES_ROOT=/path/to/System SCENARIOS=controller bash
  test/run_file_source_runtime_test.sh` loads the actual production Files
  controller fixture from System, streams 12 MiB+123 bytes through a loaded
  volume provider, unloads Files, reads/seeks in the receiver and reloads Files
  to consume its source result. Repeat with SANITIZE=1.
- `test/run_native_app_data_mount_test.py` materializes the hash-pinned vendor
  objects and tests the shared fence. `MOUNT_ROOT=$PWD MOUNT_BUILD=/its/build
  python3 test/run_native_file_source_mount_test.py` uses those actual VFS
  objects for NativeFileSource healthy, flash open/read fault and failed-close
  cases. Specify SANITIZE=1 consistently for both builds. Faults are modeled
  flash errors, never device or power-loss qualification.
- `test/compile_native_file_source_target.sh PACKAGES_DIR OUTPUT_DIR` compiles
  the Runtime/API target layout and selected source binding on Xtensa, and
  verifies the disabled binding emits no symbols.

Selected product profiles must retain the allocated `risc_file_source_abi=1`
marker with `-Wl,-u,risc_file_source_abi` and add all seven required wrappers:
`esp_vfs_register`, `esp_vfs_unregister`, `esp_partition_read`,
`esp_partition_write`, `esp_partition_erase_range`, `esp_littlefs_format`, and
`esp_littlefs_format_partition`, each as `-Wl,--wrap=NAME`.

The mount dependency is imported unchanged from 0a5e5a6548fb22725b4fec04902c5d35b72f816a
and 1b984c11be253d4fc91f81032677385080601cad. Its default-off NativeAppData
parity and actual pinned-VFS evidence are included under evidence/native-private-mount-20261012.

Internal full native builds may be used for cohort admission. Deliverables are
source and test evidence only. No flash, serial session, merge, release or
hardware qualification is part of this work.
