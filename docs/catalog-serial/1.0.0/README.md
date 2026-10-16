# Catalog / RTEF1 integration contract 1.0.0

Frozen 2026-10-16. This is a versioned integration baseline for the **published
Waveshare 0.1.52 and T-Watch 1.1.43** releases. The associated evidence is [pins.json](pins.json) and
[conformance.json](conformance.json). Its source and product-admission pins apply
to those two releases; X4 admission is outside this baseline.

## 1. Exact delivered revisions

| Product | Release / product source | Runtime source | Admission |
| --- | --- | --- | --- |
| Waveshare | [0.1.52](https://github.com/michaelrolphone-cmyk/RiscRTE-Wavshare-S3-Touch-AMOLED-2.06/releases/tag/test-shared-default-0.1.52), [3368c0719597116fcb4b583b4a596f41a79d5263](https://github.com/michaelrolphone-cmyk/RiscRTE-Wavshare-S3-Touch-AMOLED-2.06/tree/3368c0719597116fcb4b583b4a596f41a79d5263) | [9f8d67029181f6cd064e247aa949a295854fc494](https://github.com/michaelrolphone-cmyk/RiscRTE/tree/9f8d67029181f6cd064e247aa949a295854fc494) | Writable IDs 2 and 12 |
| T-Watch | [1.1.43](https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3/releases/tag/test-shared-default-1.1.43), [aa91c1cd7eb664d9a5bd78df05cab53b2c683f18](https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3/tree/aa91c1cd7eb664d9a5bd78df05cab53b2c683f18) | [21c6d64c51470158e1e8fb26d403b194f27ea38c](https://github.com/michaelrolphone-cmyk/RiscRTE/tree/21c6d64c51470158e1e8fb26d403b194f27ea38c) | Writable ID 2 only |

Both use shared Watch `8132d73c828c0e4763f6c89a3d6a4a883314056a`, System
`cb483470887b5198e2f3cd2457586ef6622142a9`, Utilities
`fc9f93402050f020a3422ee889620b2414c7ad5d`, Drivers
`5890721073ebabd781d5d278e447c761947a5c58`, and ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643` (native GCC 15.2 toolchain).
The shared `default.elf` is 617424 bytes, SHA-256
`9f10c7150cdc175ff5af0dbc1410e455ead4f18868dd56555e10860781103f2f`.
This shared application binary does **not** mean the two Runtime firmware images
are identical. Each release's firmware/store/bank binding remains separate.

The [pinned client](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/scripts/serial_files.py)
is `scripts/serial_files.py`, SHA-256
`7dd82ad4fb9d3abd953519d584b551f5735d7b25f47a000d0ebf766490123f6a`.
Its bytes are identical at both delivered Runtime revisions. The production wire
engine, its header, `RuntimeFiles.h`, `RiscStorageFilesV1.h`, `ReopenedFiles`
implementation/header, admission header, and native-storage contract also match.
Wave's later Runtime changes native workspace placement/initialization and its
qualification, not this wire/client behavior. Pins include hashes of each source.

The product's exact [Wave profile](https://github.com/michaelrolphone-cmyk/RiscRTE-Wavshare-S3-Touch-AMOLED-2.06/blob/3368c0719597116fcb4b583b4a596f41a79d5263/profiles/native-idf61.cmake)
and [T-Watch profile](https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3/blob/aa91c1cd7eb664d9a5bd78df05cab53b2c683f18/profiles/native-idf61.cmake)
select the endpoint and private AppData service explicitly; only Wave additionally
selects SD. Both published releases disable boot export. Public `source-lock.json`
is a frozen build-input record and retains its prequalification labels. Publication
and software qualification are established separately by public `release.json`,
`qualification.json`, the successful exact-source CI runs, and current release
asset digests. None of those records claims hardware testing.

## 2. Responsibility and admission boundary

RTEF1 is an owner-trusted, generic **individual-file** service. Its entire useful
surface is namespace discovery, stat, one-directory mkdir, read, exclusive create,
and explicitly requested in-place replacement. The service does not interpret
file contents, manifests, firmware versions, packages, payload associations, or
installation phases. The catalog alone owns firmware/payload association,
compatibility decisions and sequencing outside this protocol. For a paired update,
its required transferred payload must remain usable after the update; the catalog
chooses sequencing and the flash method from its manifest and explicit destination
mapping. This does not promise that every firmware installation preserves every
setting. Per-file operations preserve unrelated files and never wholesale replace
folders. Transferring bytes does not install, activate or execute them.

No device discovery, backend inference, default namespace, directory enumeration,
recursive copy, rename, delete, format, reset, firmware flashing, or transactional
replacement is introduced. The broader storage ABI contains operations that
RTEF1 deliberately does not expose. This freeze does not define a catalog payload
schema, builder instructions, or an installer implementation.

Admission is a trusted native-product decision, never an application grant or a
host option. `RISC_NATIVE_SERIAL_FILES` defaults to 0. Compiling it to 1 alone does
not admit a service: an immutable map of permanent `RuntimeFiles` clients and a
RAM-only readiness/custody predicate must be supplied. Missing admission leaves
the optional endpoint inert. IDs, labels and writable flags are copied; exact
clients, provider tables, configuration and dependencies remain pinned through
firmware lifetime, including terminal retention. See the pinned
[admission API](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/src/ports/esp32s3/NativeSerialFiles.h).

- Namespace IDs are positive opaque uint64 values, unique within one admitted
  map. They are not provider instance numbers or physical media identities.
  Labels are display metadata, not paths or routing instructions.
- ID 2, `Serial files`, is a separate `RuntimeFiles` / `ReopenedFiles` session,
  confined to the explicitly assigned private root `/appdata/.serial-files`.
  It requires paired AppData and the native AppData mount. This implementation
  fact is not a host-visible path prefix and must not be prepended by a client.
- Wave's ID 12, `SD card`, is an explicitly selected independent Runtime graph
  grant to `sdmmc-fatfs`, instance 12. Its selection is proven by product profile
  and native composition, not inferred from label or number. T-Watch admits no
  SD namespace. Missing media stays unavailable without fallback.
- Enumeration reports admitted authority, not live provider availability.
  A clean first-enable capacity/contention failure can leave private ID 2 listed
  but unavailable for that boot. Unknown IDs and missing paths differ from an
  unavailable provider.
- Neither release exposes the active boot bank. A separately selected optional
  ID 1 `Active boot store` is read-only at both admission and provider layers.
  It cannot be an upload destination. Do not add it to these release inventories.

Pinned [native composition](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/src/ports/esp32s3/NativeSerialStorage.cpp)
and [composition contract](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/docs/NATIVE_SERIAL_STORAGE.md)
are authoritative for these implementation details.

## 3. Bounds and framing

The supplied client is Python 3, standard-library-only, using a selected POSIX
serial character-device path at **115200 baud, 8 data bits, no parity, 1 stop bit**.
Use one client and stop competing readers. It does not issue DTR/RTS, reset or
flash commands. The OS/USB adapter can still alter modem lines on open/close;
that hardware behavior requires separate verification.

| Property | Frozen bound / behavior |
| --- | --- |
| Frame | Printable ASCII, LF-terminated; content shorter than 192 bytes |
| In flight | One request at a time; await its exact response |
| Session | Nonzero 32-bit value, exactly 8 lower-case hexadecimal digits |
| Sequence | Decimal 1 through 4294967295, strict increment; no zero or wrap |
| Namespace map | 0–16 entries; IDs 1 through 18446744073709551615 |
| Namespace label | 1–63 bytes; supplied client requires strict UTF-8 without controls/DEL |
| Chunk | At most 48 decoded bytes for PATH, READ and WRITE |
| Path | At most 511 strict UTF-8 encoded bytes; absolute namespace path |
| Sizes / offsets | Unsigned 64-bit wire values; actual provider/capacity limits can be lower |
| Client deadline | 5 seconds per request by default; explicit finite timeout must satisfy 0 < seconds < 30 |
| Firmware idle budget | 30000 ms for pending frames/replies/transfers, cleanup only at a safe owner checkpoint |
| Dispatch budget | At most two bounded file calls per command |

Paths reject root `/` as a command target, empty components, `.` and `..`, trailing
slashes, backslashes, NUL, ASCII controls and DEL. They are not Unicode-normalized.
Spaces/non-ASCII data are encoded in hexadecimal without loss; fragmented UTF-8
may cross PATH chunks. PATH validates the completed path at the file command.
A PATH at offset 0 resets the selected path, otherwise its offset must match the
current path length. Provider limits can be stricter. For private ID 2 the complete
VFS path must remain below 512 bytes, including the 21-byte private root, so its
namespace path is at most **490 bytes**, subject also to LittleFS component limits.
Private writes preserve 139264 bytes of private-state reserve plus a 4096-byte
metadata margin. Neither bound promises that a proposed upload fits free space.

The [wire engine](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/src/runtime/serial/Files.cpp)
and [client](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/scripts/serial_files.py)
implement these bounds. Diagnostics can interleave on the transport. The client
ignores non-RTEF1 lines, discards oversized diagnostics with bounded memory, and
does not extend the request deadline because diagnostics keep arriving.

## 4. Exact operations

Request prefix: `RTEF1 <session> <sequence>`. Reply has the same prefix followed
by a signed status and command-specific fields. Every line ends in LF. The table
shows command suffixes after the prefix; `0` is the only success status.

| Request suffix | Successful response suffix | Contract |
| --- | --- | --- |
| `HELLO` | `0 HELLO 48 511 count` | Establish a new session at sequence 1 |
| `NS index` | `0 NS id writable labelhex` | Enumerate map indexes 0 through count−1; writable is 0 or 1 |
| `PATH offset hex` | `0 PATH nextoffset` | Append 1–48 bytes at exact offset; offset 0 resets |
| `STAT namespace` | `0 STAT flags size` | Selected path's raw generic flags and byte size |
| `MKDIR namespace` | `0 MKDIR` | Create exactly one directory; not missing parents |
| `GET namespace` | `0 GET size` | Open selected existing file for read |
| `READ offset` | `0 READ nextoffset hex` | Up to 48 bytes at expected offset; legal short read |
| `CLOSE` | `0 CLOSE crc32hex` | Fully read transfer only; successful provider close and CRC32 |
| `PUT namespace create size` | `0 PUT` | Exclusive creation; an existing destination fails |
| `PUT namespace replace size` | `0 PUT` | Truncate the selected existing file immediately; absent destination fails |
| `WRITE offset hex` | `0 WRITE nextoffset` | 1–48 bytes at expected offset; legal short write |
| `COMMIT crc32hex` | `0 COMMIT` | Full declared size plus correct CRC32, provider sync, successful close |
| `ABORT` | `0 ABORT` | Close known transfer and clear path; no rollback or deletion |

`READ` at EOF may omit the hex token. The supplied client uses GET's exact size,
so it does not need an extra EOF read, including for empty files. STAT flag 1 is
DIRECTORY, 2 is MODIFIED_VALID, 4 is MODIFIED_LOCAL. RTEF1 does not expose the
ABI's modified timestamp field. Preserve unknown flag bits rather than assigning
new meanings. A flat provider may return UNAVAILABLE for mkdir.

### Status values

| Code | Meaning |
| --- | --- |
| 0 | OK |
| 1 | NOT_FOUND: authoritative missing path / unknown namespace (NS index out of map also returns this) |
| 2 | END: generic directory EOF; no directory enumeration operation is exposed by RTEF1 |
| 3 | EXISTS: destination already exists |
| −1 | INVALID request/path/operation |
| −2 | IO error |
| −3 | UNAVAILABLE service |
| −4 | BUSY |
| −5 | DENIED / read-only |
| −6 | RETAINED: terminal uncertain custody until real restart |
| −7 | CONTEXT: wrong owner context |
| −9 | NO_SPACE: known-custody capacity/quota failure |
| −10 | Sequence disagreement |
| −11 | Integrity disagreement |

There is no assigned −8 in this contract. Codes through −9 and positive results
come from the pinned [storage ABI](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/sdk/app/RiscStorageFilesV1.h);
−10/−11 are protocol results. Any nonzero or unknown status is failure, even when
command-specific fields acknowledge some progress. Do not map unavailability,
retention or I/O error to NOT_FOUND. A clean BUSY/UNAVAILABLE/DENIED is not itself
terminal retention; it is also not completion.

## 5. Byte accounting, integrity and recovery

Upload requires a known-size regular source, which must not change during the
transfer. Early EOF and extra bytes fail. Advance offsets and CRC32 only by the
acknowledged remote byte count; continue a legal short write with the unconsumed
suffix. Continue legal short reads and local writes by their actual counts.
Zero progress before completion is an error. Invalid provider transfer counts
fence custody. CRC32 is standard IEEE CRC32 (polynomial `0xedb88320`, initial/final
XOR `0xffffffff`), compatible with Python `zlib.crc32`, encoded as eight lower-case
hex digits. It covers exactly the transferred bytes, not metadata or paths.
It detects accidental corruption and provides no authentication, MAC or encryption.

An upload is successful only after acknowledged COMMIT validates count/CRC,
synchronizes the provider and closes successfully. A download is successful only
after count/CRC match and successful CLOSE; the CLI also flushes and fsyncs the
local regular file before reporting success. Provider completion is not proof of
hardware durability or immunity to later power loss.

Replace is **in-place and destructive at PUT**, before the first WRITE. It is not
atomic, cannot create a missing file, and has no staging rename or recovery log.
Failure can leave that selected file empty, partial or unverified. It does not
replace directories or siblings. Default create is exclusive. For downloads,
local create is exclusive by default; `get --replace` explicitly permits local
truncation. Final symlinks and special destinations are rejected. Failed local
files are neither silently deleted nor reported as verified.

A definitive transfer error permits the client's best-effort ABORT for a known
handle. ABORT does not remove a partial create, restore a replaced file or roll
back prior writes. On timeout, disconnect, malformed reply, wrong session,
sequence, offset or other uncertain response, the client becomes unusable, sends
no cleanup request and performs **no automatic retry, reconnect or reset**.
A lost PUT acknowledgment may mean truncation occurred; a lost COMMIT acknowledgment
may mean the file completed. Inspect the selected destination/provider state before
choosing another transfer. Host port close or elapsed time proves neither cleanup
nor rollback. Protocol −6 and −10 also make the client session unusable.

Firmware caches only the latest exact request/response pair. An immediate exact
duplicate replays without repeating I/O; changed contents at the same sequence
fail. This bounded replay is not a verified-success or automatic-retry guarantee.

## 6. Owner checkpoint and retained custody

Transport RX parsing and partial TX are RAM-only through the diagnostic owner.
Provider I/O runs only at a settled resident foreground checkpoint, the explicit
size-checked Runtime `service_checkpoint` suffix, or after Runtime::run safely
returns. Ordinary yield, capability acquisition, panel BUSY polling and provider
callbacks do not dispatch synchronous serial storage.

The [Runtime checkpoint implementation](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/src/bootstrap/Runtime.cpp)
checks owner/app-main context, native idleness, graph lifecycle, streams and
storage custody, temporarily excluding app entry for the call. BUSY leaves work
queued. RETAINED is terminal. The optional
[compatibility helper](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/9f8d67029181f6cd064e247aa949a295854fc494/sdk/app/RiscRuntimeCheckpointV1.h)
makes a missing older suffix a safe no-op. The delivered shared Home's
[settled-frame regression](https://github.com/michaelrolphone-cmyk/RiscRTE-Watch/blob/8132d73c828c0e4763f6c89a3d6a4a883314056a/scripts/test_runtime_service_checkpoint.py)
checks the call after completed presentation/callbacks, outside display BUSY waits.

A healthy open transfer temporarily fences sleep, USB PHY handover and normal
restart; it is not terminal retention. Private storage holds one logical handle,
reopens per bounded operation under exact mount/resource guards, checks size and
cursor, syncs writes, then closes before releasing guards. No physical file handle
or lease is held between healthy host commands. The private root is inaccessible
to ordinary application/settings/file-source/package sessions. This exclusive
ownership is required: size checks do not detect an out-of-contract same-size
mutation, and links/independent writers are not supported by this private provider.

App/graph teardown closes known serial handles before dependencies are released.
Failed close, invalid transfer count, loss of owner/safety during an operation,
uncertain private-provider I/O or RETAINED fence future I/O and keep dependencies
and necessary custody pinned until a real restart. A merely unsafe/busy checkpoint
performs no I/O; it is distinct from losing custody during an admitted operation.
Cancellation without an open handle is RAM-only and can proceed while a provider
is unavailable. Closing an open inactive transfer requires a safe checkpoint;
the 30-second idle budget may not yield immediate cleanup. No autonomous device
reset is part of this service or its client recovery.

## 7. Qualification and handoff

[conformance.json](conformance.json) keeps source verification, software tests,
publication and hardware evidence distinct. Public release assets include
`TESTING.txt`; both releases remain **hardware untested**. Their factory images
erase existing device data, and no data-preserving update kit is supplied. Per-file serial updates preserve unrelated files and directories.
RTEF1 transfer does not flash factory artifacts.

For later authorized device testing: keep Home awake, use one selected owner-trusted
115200 8N1 client, enumerate actual admitted IDs, exclusively create a disposable
file, read back/compare, then explicitly replace that same file. Exercise binary,
empty, partial/interrupted cases and preserve siblings; inspect failures before
another transfer. Follow the release TESTING.txt and do not infer SD availability
from map presence.

The evidence records exact published pins, source equivalence, admission review,
normal/ASan+UBSan host conformance, successful exact-source upstream CI and verified
publication digests. Hardware timing, USB behavior and physical media durability
remain untested.

### Recheck the evidence bundle

Run `python3 verify_freeze.py` from this directory to verify the committed logs
and internal evidence consistency. To additionally rehash source and published
assets, supply `--waveshare-runtime`, `--twatch-runtime`, `--waveshare-assets` and
`--twatch-assets` directories containing the exact frozen files.
