# Read-only admitted app catalog v1

`app.catalog@1` is an explicitly granted native app capability. The application
must declare it and the owner must grant API 1, instance 0. The table in
`sdk/app/RiscAppCatalogV1.h` has only `snapshot` and generation-bound `get`.
It does not expose the installation coordinator, bank store, package admission,
filesystem enumeration, or update authority. Existing runtime APIs are unchanged.

## Publication

The existing native boot/removable admission path validates and copies these
manifest fields into the policy's metadata allocation:

- `display_name`: bounded UTF-8 string; the admitted `id` is used when absent.
- `icon`: bounded UTF-8 string; empty when absent.
- `min_firmware_version`: bounded descriptive string; empty when absent. This
  field does not add firmware compatibility or update authority.
- `launcher_visible`: optional Boolean, default `true`. False omits the entry
  from this presentation catalog only. Installed inventory, existing app
  permissions, and launch authority are preserved.

Unusable optional presentation strings (overlong, embedded-NUL, invalid-UTF-8 or
control-containing) normalize to the admitted ID for the label and empty strings
for icon/minimum version. Only an explicit Boolean false hides a row. Existing
native identity, declaration and file-association validation remains unchanged;
the catalog does not introduce additional startup/admission rejections for old
presentation metadata. `id` copies the validated native ID. `file_name` has a
256-byte field matching the native policy identity capacity and comes from the exact
admitted launch path relative to the native authority root, including directories
for installed apps; it is not reconstructed from a display name or product list.
Dormant catalog-authority reservations and retired policies are omitted.

Both callbacks inspect only admitted RAM metadata. They do not allocate, reopen
manifests, enumerate directories, activate providers, or trigger media discovery.
Native discovery and admission remain the publication owner. A missing manifest
at admission fails closed; deleting a manifest after admission does not rewrite
the cached publication. A successful empty publication is valid.

The nonzero generation advances on admitted metadata changes and on observed
visibility/launch-readiness changes. Get refreshes that RAM-only state before
comparing the caller's generation. Old generations return `STALE`; out-of-range
indices return `NOT_FOUND`. Rejected admission preserves the prior metadata.
A successful tentative native admission followed by transaction rollback may
conservatively invalidate an older generation. Generation exhaustion is terminal
`UNAVAILABLE`, never wraparound reuse.

`AVAILABLE` reflects the existing native launch-readiness decision, including
installed settings readiness. It is not a fresh physical-media check or a promise
that loading will succeed. Native retirement removes entries; a settings
readiness transition changes their flag and stales earlier generations.

## Custody

One catalog grant may be live per invocation. Opaque contexts come from the
Runtime's non-recycled native context sequence. Grant release, child handoff,
invocation return and shutdown invalidate copied contexts. Callbacks fence owner
task, invocation, grant lifetime, reentry and native retention before copying
outputs. Errors leave caller output untouched. `CONTEXT` and `RETAINED` are
terminal custody failures; consumers must not call or release the table again.
Ordinary `INVALID`, `UNAVAILABLE`, `STALE` and `NOT_FOUND` remain releasable.

Acquisition holds a reentry guard across native safety callbacks and rechecks
owner/invocation custody before publishing the grant. A failed native safety
barrier retains the invocation rather than reporting an optional missing service.

Metadata uses the existing policy allocation and selected memory class; limits,
profiles, storage recovery, transport and hardware configuration are unchanged.

## Host checks

Run `test/run_app_catalog_runtime_test.sh` and repeat with `SANITIZE=1` for
AddressSanitizer/UndefinedBehaviorSanitizer. The runner covers all four combinations
of atomic key-value and native settings lifecycle selection. The fixture loads actual host app and
provider modules through Runtime. It covers explicit admission, denied authority,
25 installed/23 visible without filename filtering, nested directory identities,
legacy long-route preservation, optional metadata normalization, malformed/missing
identity metadata, native admitted inventory changes, settings-readiness
changes, stale grants, reentry, invocation replacement, retention and zero catalog
filesystem calls. These checks are source/host evidence, not target or hardware
qualification.

`test/run_atomic_key_value_runtime_test.sh` also holds real atomic settings and
catalog grants together and verifies independent tables, reads and revocation.
Repeat with `NATIVE_SETTINGS_LIFECYCLE=1` for the combined production features.
See [the dispatch collision regression](APP_CATALOG_ATOMIC_DISPATCH.md).
