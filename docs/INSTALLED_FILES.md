# Explicit installed-file browsing (0.1.28)

`storage.installed-files@1`, authorized with `instance_id: 0`, is a narrowly
brokered read-only view using the existing `risc_storage_volume_api_v1` layout.
An app must declare the capability and receive the exact explicit boot grant.
It does not receive `storage.volume` authority or raw platform access implicitly.

Only the exact manifest and ELF paths of already-admitted app policies and
selected providers are visible. Directory names are derived from this inventory.
The Runtime never enumerates the boot filesystem. `boot.json`, board metadata,
owner provisioning descriptors, credentials, arbitrary private service state,
NVS and unselected files are not exposed. A suffix or file extension alone never
admits a file. Every selected manifest has passed its existing strict parser.

All paths are normalized, relative to the immutable installed root, with no dot
segments, repeated/trailing separators, backslashes, control characters or
oversized components. Host POSIX symlinks in the root or any path component are
rejected; SPIFFS has no symlinks. Read callbacks re-open only the admitted file,
check identity before/after bounded reads, and copy data only after success.
Each native read is at most 512 bytes. Handles preserve an offset and stat
identity; normal replace/resize/in-place metadata changes return a stale/error
instead of mixing versions. POSIX identity includes nanosecond modification and
change times. SPIFFS metadata is coarser, so target coherence additionally relies
on the existing immutable active-bank rule: all supported updates stage another
bank, never rewrite this mounted active installation. This is not a guarantee
against arbitrary trusted native code mutating flash outside the Runtime.

Successful callbacks leave no native file descriptors open. Unbuffered reads prevent hidden stdio prefetch. A failed native close latches retention: no new I/O, grant release, launch, app teardown or sleep proceeds, and no potentially retired descriptor is retried. Restart is required to recover that state. Logical file/directory
handles increase monotonically and are retired on close, release and app exit.
Copied callback/context pairs require the current invocation, owner task,
exact live grant generation and storage-safe state. One volume grant is live at
a time; children do not inherit it. All write/create/remove pointers are null.
There is no format, partition, filesystem repair, erase or executable-dispatch
operation. End-of-directory clears the error; other read failures are explicit.

## Capacity without fixed-RAM growth

The current Watch app cohort needs 18 policies and 11 distinct capability types;
the existing 12 grant slots still cover LoRa's two explicitly independent KV
namespaces. Policy metadata is allocated only for the actual validated count.
Paired and metadata-PSRAM targets retain the existing PSRAM-only policy with no
internal-memory fallback. Allocation failure rejects preparation. The metadata
outlives graph destruction and retained grant-name references. The existing
store audit bound is 128 files; three added app pairs remain within that bound.

## Tests and limits

`bash test/run_installed_files_test.sh` tests the model and actual dynamic ELF
through the production Runtime. `SANITIZE=1` enables ASan/UBSan. Tests cover private
file exclusion, nested inventory folders, path aliases/traversal/symlinks,
bounded reads and EOF, replacement/in-place stale detection, retired handles,
explicit grant/version/instance checks, owner checks, fresh child/default reload
and copied-context revocation. Existing policy tests cover 18 accepted, 19 rejected,
last-slot identity/grants, parser-memory churn and failed reprepare.

These are host/model and target-build checks, not device/storage/power-loss
qualification. General recoverable rename/move/trash for user files remains a
separate user-volume dependency. The Watch's current paired layout has no
writable user-data filesystem; this work does not change that layout or pretend
shared 24 KiB NVS is an unbounded file store.

The VFS contract model normalizes stat/fstat like the [official IDF 4.4 SPIFFS adapter](https://github.com/espressif/esp-idf/blob/v4.4.7/components/spiffs/esp_spiffs.c), and injects failed stat, seek, read and close calls. It verifies no partial output or retained descriptors. This is a model of those callbacks, not execution on physical SPIFFS. A private compile-only test seam injects failure at policy, inventory scratch and retained-inventory allocation; preparation and retry fail closed.

The real Runtime/ELF fixture also injects a native close failure and verifies retained invocation/graph lifetime, denied release/handoff, unchanged output and revoked callbacks. This deliberately models an unconfirmed close even when the host wrapper has closed its descriptor.

A failed fclose does not leave a reusable FILE object. The underlying VFS may have retired its descriptor or may have failed before retirement; retrying the numeric descriptor could close a reused descriptor belonging to another owner. The Runtime therefore latches the outcome and requires restart. The exit barrier precedes app fini (asserted by the actual ELF fixture), so it leaves the File Browser's last submitted restart-needed frame intact rather than tearing down the display. If the physical display itself is broken, only diagnostics remain; no physical-display guarantee is claimed.
