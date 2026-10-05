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
Each native read is at most512 bytes. Handles preserve an offset and stat
identity; normal replace/resize/in-place metadata changes return a stale/error
instead of mixing versions. POSIX identity includes nanosecond modification and
change times. SPIFFS metadata is coarser, so target coherence additionally relies
on the existing immutable active-bank rule: all supported updates stage another
bank, never rewrite this mounted active installation. This is not a guarantee
against arbitrary trusted native code mutating flash outside the Runtime.

No native file descriptors remain open across callbacks. Logical file/directory
handles increase monotonically and are retired on close, release and app exit.
Copied callback/context pairs require the current invocation, owner task,
exact live grant generation and storage-safe state. One volume grant is live at
a time; children do not inherit it. All write/create/remove pointers are null.
There is no format, partition, filesystem repair, erase or executable-dispatch
operation. End-of-directory clears the error; other read failures are explicit.

## Capacity without fixed-RAM growth

The current Watch app cohort needs18 policies and11 distinct capability types;
the existing12 grant slots still cover LoRa's two explicitly independent KV
namespaces. Policy metadata is allocated only for the actual validated count.
Paired and metadata-PSRAM targets retain the existing PSRAM-only policy with no
internal-memory fallback. Allocation failure rejects preparation. The metadata
outlives graph destruction and retained grant-name references. The existing
store audit bound is128 files; three added app pairs remain within that bound.

## Tests and limits

`bash test/run_installed_files_test.sh` tests the model and actual dynamic ELF
through the production Runtime. `SANITIZE=1` enables ASan/UBSan. Tests cover private
file exclusion, nested inventory folders, path aliases/traversal/symlinks,
bounded reads and EOF, replacement/in-place stale detection, retired handles,
explicit grant/version/instance checks, owner checks, fresh child/default reload
and copied-context revocation. Existing policy tests cover18 accepted,19 rejected,
last-slot identity/grants, parser-memory churn and failed reprepare.

These are host/model and target-build checks, not device/storage/power-loss
qualification. General recoverable rename/move/trash for user files remains a
separate user-volume dependency. The Watch's current paired layout has no
writable user-data filesystem; this work does not change that layout or pretend
shared24KiB NVS is an unbounded file store.
