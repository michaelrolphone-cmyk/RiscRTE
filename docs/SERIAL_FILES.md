# Individual-file serial transfer

`scripts/serial_files.py` is a standard-library-only Python 3 client for the
optional `RTEF1` file endpoint. It streams one explicitly selected file at a time.
It does not discover devices, reset boards, flash firmware, install applications,
format storage, remove files, synchronize trees, or select a physical backend.

The endpoint is disabled by default (`RISC_NATIVE_SERIAL_FILES=0`). A product
must compile it in **and explicitly admit immutable namespace sessions** at a
settled Runtime owner boundary. An enabled transport without an admitted service
is unavailable. Ordinary builds and the separate owner-installation protocol do
not acquire writable access through this client. Enabling the service is a
trusted firmware/product decision, not a command-line permission or application
capability. See `src/ports/esp32s3/NativeSerialFiles.h` for that admission contract.

## Native product admission and scheduling

An opt-in native product adds `RISC_NATIVE_SERIAL_FILES=1` to its build
selection and implements the weak `risc_native_serial_files_admission()` getter.
It returns `RiscCpu::NativeSerialFiles::Admission`: an immutable map of positive
opaque namespace IDs, labels, exact `RiscBoot::RuntimeFiles` clients, explicit
writable flags, and a RAM-only readiness/custody predicate. At most 16 namespace
entries and 63 label bytes are supported. The adapter copies IDs/labels/flags;
clients, provider tables, configuration and dependencies remain pinned for the
whole firmware lifetime. No default namespace, filesystem root, media type,
mount, format, or provider acquisition is inferred. Missing admission leaves
the optional endpoint inert without changing ordinary boot.

The map is trusted native authority. Never supply an invocation-bound application
grant, or blindly reuse a metadata/source client whose open handles make an
existing app-exit barrier fail. A product must explicitly qualify its permanent
session's owner, resource arbitration and lifecycle. Provider availability is
reported through its file status; the readiness callback establishes whether
it is safe to dispatch at all, without I/O, acquisition or callbacks.

### Explicit built-in product storage composition

The optional built-in composition selects independent sessions with
`RISC_NATIVE_SERIAL_BOOT_FILES=1` (read-only active boot store, wire ID 1),
`RISC_NATIVE_SERIAL_APPDATA_FILES=1` (private `/appdata/.serial-files`, wire ID 2),
and `RISC_NATIVE_SERIAL_SD=1` (wire ID 12). All require
`RISC_NATIVE_SERIAL_FILES=1`; private files also require the selected fenced
AppData mount. These wire IDs are logical authority, never provider instance IDs.
They do not expose app-owned AppData roots or give apps a new capability.

For SD, the product can override **both** compile-time selectors:

```text
RISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"
RISC_NATIVE_SERIAL_SD_INSTANCE=9
```

This is the explicit X4 candidate mapping. Omitting both selectors preserves
the existing Waveshare `sdmmc-fatfs`, instance 12 composition. Supplying only
one selector, an empty/embedded-NUL/oversized provider string, or a noninteger
or out-of-range hardware instance rejects compilation. A complete override
never falls back to the legacy selection. Board identity, chip compatibility,
media paths, labels and application grants do not select the provider.

The actual startup calls `NativeSerialStorage::bindSd` only after complete
Runtime graph preparation and serial storage preparation. It asks the unchanged
native binding gate for the exact provider and instance with `storage.volume@1`
and explicit one-time volume preparation. Missing/unadmitted, wrong-API,
wrong-capability and ambiguous selections fail closed before activation.
Clean absent media remains catalogued but unavailable; failed preparation,
close or graph release with uncertain custody retains the existing terminal
fence. An X4 `NS` reply still advertises ID 12, regardless of instance 9.

`test/run_native_serial_product_storage_test.py` exercises this shared startup
path, Runtime graph admission, wire enumeration/transfers and terminal custody
for explicit `X4SD9` and default `legacyWave12`. Its provider and media are
synthetic; it does not qualify the X4 SD driver or physical device. The separate
pinned Waveshare RAM-card test also uses the shared startup binding. The native
compiler test checks the exact X4 selectors with all three storage destinations,
external workspace placement and empty default-off objects. A complete linked
product image still needs its own IQ-bank/static-memory proof and device gates.

Console parsing and partial TX run through the existing diagnostic owner and
never call providers. Requests dispatch at a settled resident foreground
checkpoint, an explicitly called size-checked `service_checkpoint` Runtime
suffix, or after `Runtime::run` has safely returned. Ordinary yield and
capability acquisition never dispatch synchronous serial storage. The explicit
suffix is app-main/owner-only and checks native idleness, graph lifecycle,
stream and storage custody before temporarily revoking app entry for the call.
BUSY means work is deferred or an admitted native session remains active; a
command may already have completed. Callers continue foreground work and defer
automatic idle until a later settled checkpoint reports OK. RETAINED is terminal.
`RiscRuntimeCheckpointV1.h` makes a missing older suffix a safe no-op. Shared Home calls it only between completed
presentations, never from panel BUSY polling. Products must still qualify their
selected provider and actual scheduling.

A command makes at most two bounded file calls. An admitted RTEF1 session
holds bounded RAM custody across request/response gaps, including after NS,
PATH, CLOSE and COMMIT. It blocks sleep, USB PHY handoff and normal restart
until explicit disconnect/shutdown or 30 seconds of inactivity are handled at
an owner checkpoint. A connected cable alone holds no session or sleep lease.
This healthy session custody is never terminal retention. App/graph teardown
closes known handles before releasing anything. Failed close, invalid provider transfer count, lost owner/safety or
`RETAINED` fence future I/O and pin dependent custody through a real restart.
Partial frames, queued replies and open transfers have a 30-second idle budget;
RAM-only cancellation works even when a provider is temporarily unavailable.
Neither scheduling nor these checks establish hardware timing/durability.

## Native workspace placement

Native profiles with `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` reserve the
owner-task protocol workspace and private `ReopenedFiles` provider in external
BSS. They require the SDK's `CONFIG_SPIRAM_BOOT_INIT` selection, already present
in the shared native defaults and both selected watch profiles. No new PSRAM
requirement applies to feature-off or non-external-BSS builds: their storage
remains internal. There is no heap allocation or internal-heap fallback.

Only raw, aligned bytes are reserved. The settled owner `prepare()` boundary
explicitly constructs each object after SDK memory initialization, preserving
nonzero constructor defaults. Admission/owner/dispatch gates, provider clients,
and mount/resource custody stay internal. No workspace access occurs before
construction; absent or rejected admission remains inert. Prepared inspection
and checkpoint calls reject foreign-task/ISR access conservatively before
reading the workspace. Selection stays
one-shot on failure and after terminal retention, so live provider state cannot
be reconstructed, released, or retried. These workspaces are ordinary task data,
never ISR, DMA, atomic, panic, or cache-disabled callback state.

The serial-enabled Waveshare verification initially placed `.dram0.bss` at
`0x3fcaa138..0x3fcb0bc0`, overlapping the fixed IQ bank at `0x3fcb0000` by 3008
bytes. Conditional external placement moves 2792 protocol bytes and 760 provider
bytes into PSRAM; the verified successor link ends internal BSS at `0x3fcafdf0`
(528 bytes before the bank). The IQ bank, its alias, application-bank geometry,
shared ABI, PSRAM SDK policy, and fail-closed overlap proof are unchanged. This
small static margin must continue to be checked on every exact native link;
it is not a claim about dynamic free heap or hardware qualification.

Regression commands (also run with `SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`):

```sh
IDF_PATH=/path/to/pinned/esp-idf bash test/run_native_serial_files_transport_test.sh
python3 test/run_native_serial_storage_test.py
python3 test/compile_native_serial_files_target.py build/compile_commands.json build/serial-objects
python3 test/prove_native_serial_workspace.py build/riscrte_native.elf \
  --workspace endpoint --workspace provider --external 1
```

Host tests cover internal/external placement, poisoned pre-construction storage,
rejected admission, copied metadata, real Console partial TX, private LittleFS
custody/faults, and retained-state reentry. Native object and final-ELF checks
verify placement independently of source attributes. The ordinary final native
IQ proof still rejects any static/load-segment overlap with either bank alias.

## Owner commands

Use an explicit POSIX serial character-device path. The transport uses 115200
baud, 8 data bits, no parity, and one stop bit. Stop other readers of that same
serial port before using it. The client never issues reset, flash, DTR, or RTS
commands; an operating system or USB serial driver may still alter modem lines
on opening/closing a device. Verify that behavior for the selected board and
adapter before using an important live session.

```sh
python3 scripts/serial_files.py --device /dev/ttyACM0 namespaces
```

Use the numeric ID returned by the product's admission map. Labels describe
namespaces and must not be treated as mount paths or inferred storage devices.
The following commands assume that the owner has selected an admitted ID `7`:

```sh
# Create one remote file exclusively. An existing destination is an error.
python3 scripts/serial_files.py --device /dev/ttyACM0 put \
  --namespace 7 --path '/notes/example.bin' ./example.bin

# Explicitly truncate and replace one EXISTING remote file.
python3 scripts/serial_files.py --device /dev/ttyACM0 put \
  --namespace 7 --path '/notes/example.bin' ./example.bin --replace

# Download to a new local file. Existing local files are preserved by default.
python3 scripts/serial_files.py --device /dev/ttyACM0 get \
  --namespace 7 --path '/notes/example.bin' ./download.bin

# Explicitly allow truncating the selected local destination.
python3 scripts/serial_files.py --device /dev/ttyACM0 get \
  --namespace 7 --path '/notes/example.bin' ./download.bin --replace

python3 scripts/serial_files.py --device /dev/ttyACM0 stat \
  --namespace 7 --path '/notes/example.bin'
python3 scripts/serial_files.py --device /dev/ttyACM0 mkdir \
  --namespace 7 --path '/notes'
```

`mkdir` creates one directory, not missing parents. A provider may report it as
unavailable (for example, a flat namespace). `stat` reports type, byte size and
raw generic file flags. Namespace enumeration is not directory enumeration.
There is deliberately no delete, rename, recursive copy, or format command.

Paths are absolute, strict UTF-8, and at most 511 encoded bytes. Empty components,
`.`/`..`, trailing slashes, backslashes, NUL, ASCII controls, and DEL are rejected.
`/` is not a selectable file/directory command target. Paths are neither Unicode
normalized nor interpreted as host filesystem paths. Non-ASCII names and spaces
are encoded losslessly. Filesystem/provider limits may be stricter.

## Completion and failure semantics

- Uploads and downloads use bounded 48-byte buffers. Legal short remote reads,
  remote writes and local writes are continued using acknowledged byte counts.
- Uploads require a source of known size. The CLI accepts a regular file, checks
  early EOF and extra bytes, and sends CRC32 for exactly the acknowledged bytes.
  Do not change the source during upload; this is not a snapshot mechanism.
- Successful upload means the endpoint accepted CRC32, synchronized the selected
  provider handle, and closed it successfully. Successful download means the
  byte count and endpoint CRC32 agree; the CLI also flushes and syncs the local
  destination before printing success. CRC32 detects accidental corruption, not
  malicious data modification or peer identity.
- `put --replace` is **in-place and destructive at PUT**, not atomic. It truncates
  the selected existing file before the first data chunk arrives. It does not
  create a missing destination. Create mode uses exclusive creation.
- `get` creates a new local file exclusively by default. `get --replace` can
  truncate an existing local regular file. A final local symlink or special file
  is rejected. Failed downloads may leave an empty, partial or unverified local
  file; it is never silently deleted or passed off as a verified copy.
- A definitive transfer error permits a best-effort `ABORT` to close the known
  remote transfer. ABORT does not remove a partially created file, restore a
  replaced file, or guarantee that earlier writes were rolled back.
- A timeout, disconnect, malformed response, wrong session/sequence, wrong
  offset, or otherwise uncertain wire outcome makes the client session unusable.
  No request is automatically retried, and no cleanup request is sent after that
  ambiguity. A lost COMMIT acknowledgment can mean the file committed correctly;
  a lost PUT acknowledgment can mean the destination was already truncated.
  Inspect the selected destination and provider state before choosing a new
  transfer. The client never reconnects or resets the device for recovery.
- Missing paths, existing files, unavailable services, denied/read-only access,
  busy providers, no space, integrity failure and retained custody are distinct
  statuses. Retained custody is terminal until a real restart and is never
  converted into an ordinary missing-file or retryable result.
- Firmware expires an inactive session after 30 seconds at an owner checkpoint.
  The client must establish a fresh HELLO session after expiry; stale session/
  sequence requests receive protocol error -10. ABORT cancels the current
  transfer, but does not end the session. An open transfer is closed only at its
  safe owner checkpoint. Disconnect/timeout cleanup can itself fail or be delayed
  until that checkpoint. Neither host serial-port close nor time passing proves
  cleanup, rollback, or completion.

The default deadline is five seconds per request, including interleaved runtime
diagnostics. Set `--timeout` before the subcommand to a positive value below
30 seconds if a known provider needs more time. Slow diagnostic output cannot
extend the deadline. Oversized diagnostic lines are discarded with bounded
memory; oversized or malformed protocol responses fail the session.

## RTEF1 wire contract

ASCII requests and replies are newline terminated and shorter than 192 content
bytes. The client sends one request and awaits its exact reply before sending
another. Each new client chooses a random, nonzero, eight-digit lower-case
hexadecimal session ID. Sequence numbers are decimal, start at 1, and increase
by one through the session; zero, wraparound and out-of-order requests fail.

```text
request: RTEF1 <session> <sequence> <COMMAND> [arguments]\n
reply:   RTEF1 <session> <sequence> <status> [COMMAND data]\n
```

Status zero is success. Other status values use `RiscStorageFilesV1.h`, plus
`-10` for sequence disagreement and `-11` for integrity disagreement. Error
replies may contain command-specific progress data; nonzero status is never
successful completion even if some bytes were transferred.

| Request | Successful reply suffix | Meaning |
| --- | --- | --- |
| `HELLO` | `0 HELLO 48 511 count` | Establish new session at sequence 1 |
| `NS index` | `0 NS id writable labelhex` | Enumerate admitted namespaces |
| `PATH offset hex` | `0 PATH nextoffset` | Append <=48 path bytes; offset 0 resets |
| `STAT namespace` | `0 STAT flags size` | Stat the completed selected path |
| `MKDIR namespace` | `0 MKDIR` | Create one selected directory |
| `GET namespace` | `0 GET size` | Open selected file for reading |
| `READ offset` | `0 READ nextoffset hex` | Read <=48 bytes at the expected offset |
| `CLOSE` | `0 CLOSE crc32hex` | Close a fully read file and return CRC32 |
| `PUT namespace create size` | `0 PUT` | Create selected file exclusively |
| `PUT namespace replace size` | `0 PUT` | Truncate selected existing file |
| `WRITE offset hex` | `0 WRITE nextoffset` | Write <=48 bytes; short writes legal |
| `COMMIT crc32hex` | `0 COMMIT` | Verify full size and CRC32, sync, then close |
| `ABORT` | `0 ABORT` | Close transfer and clear path, without rollback |

`READ` at EOF can return `0 READ nextoffset` without a hex token. The client
knows the size from GET and does not need an extra EOF read, including for an
empty file. CRC32 uses the standard IEEE algorithm compatible with Python
`zlib.crc32`, represented as eight lower-case hexadecimal digits. No credentials,
MAC, encryption, peer authentication, transactional rename, or durable recovery
log are provided by this protocol; the selected transport is an owner-trusted
channel.

The firmware caches the most recent exact request/response pair. An exact
immediate duplicate can replay that response without repeating provider I/O.
A changed frame using the same sequence fails. This bounded replay mechanism
does not make a lost response a verified success, so the CLI deliberately does
not automatically resend mutating or uncertain transfers.

## Host verification

```sh
./test/run_serial_files_client_test.sh
SANITIZE=1 ./test/run_serial_files_client_test.sh
```

The runner builds `test/serial_files_wire.cpp` with the production
`src/runtime/serial/Files.cpp`, `RuntimeFiles`, and `PosixFiles`. The Python client
launches it through anonymous stdin/stdout pipes and temporary host filesystem
roots. No hardware, serial devices, discovery, reset, flashing, or live provider
admission is used. Sanitizer mode enables ASan and UBSan on the C++ endpoint. In a traced
executor where LeakSanitizer reports that it cannot operate under ptrace, run
`ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 ./test/run_serial_files_client_test.sh`;
that explicitly excludes leak detection and must not be reported as an LSan pass.

Coverage includes binary/empty/chunk-boundary round trips, fragmented UTF-8
paths, exclusive creation and explicit replacement, read-only/missing/unavailable
namespaces, legal short I/O, write/sync failures, source/sink failures, CRC
corruption, mismatched offsets/session/sequence, duplicate-frame replay, lost
PUT/COMMIT acknowledgments, bounded diagnostic handling, and local destination
protection. These tests verify the host client/wire/provider contract; they do
not qualify USB behavior, hardware timing, target storage durability, or a
particular product's namespace admission and owner-boundary scheduling.
