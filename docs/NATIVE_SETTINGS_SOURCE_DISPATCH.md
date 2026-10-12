# Checked native settings source dispatch

This is a default-off Runtime source increment. It selects no product profile,
changes no Watch policy, and does not qualify a device, physical card or flash.
It builds on the checked fresh/import receipts and SD restoration protocols.

## Pre-app selection

`NativeSettingsBootstrap` holds the existing global native lease through a
worker-side complete inventory, owner-side SD catalog work when needed, and
worker-side admission/publication. A successfully acquired SD attempt remains
fenced through native publication, including the vacant-catalog handoff. The
owner performs no NVS or native file I/O.
The factory and its initialization capability occupy permanent bootstrap control
storage until collection; retained or uncertain publication preserves custody.

- A selected native root loads normally without requiring SD.
- An exact `RTENFI1` fresh receipt resumes through `admitFreshResume` and the
  matching complete fresh/owned-attempt inventory. It does not require SD.
- An exact `RTENLI1` import receipt resumes through `admitImportedResume` and the
  matching complete original-source/owned-attempt inventory. It does not require SD.
- An exact `RTENSI1` SD receipt requires the corresponding checked SD restoration
  candidate. It cannot become a fresh or legacy admission.
- A missing root with complete unanchored legacy data is materialized using the
  shipped native protocol decoder. A readable populated SD catalog is an explicit
  conflict, not a merge choice. Verified SD vacancy permits import. If SD cannot
  be admitted or refreshed, independently complete native legacy data can still
  be imported and used; its unknown SD lineage remains for later reconciliation.
- A missing root with complete native absence can restore a qualified SD
  checkpoint, or initialize FreshEmpty after a verified vacant SD catalog.
  Unavailable/busy/failed SD does not establish vacancy and cannot authorize new
  persistent defaults.
- Unknown native metadata, bare `RTENINIT`, malformed rows, unsupported anchored
  legacy journals, unknown files, inventory errors and changed original sources
  refuse without source deletion. A collector's `Missing` result may only select
  the independently checked fresh inventory, never substitute for it.

Legacy rows, full journals and deltas are preserved byte-for-byte, including
shadowed values covered by their fingerprint. Imported provenance retains its
archive obligation. Native initialization does not acknowledge an SD generation.
`Synchronized` in the existing bootstrap result enum denotes successful bootstrap
completion; the Core's checked identity remains authoritative for SD status.

`NativeSettingsAssembly::Backend::legacy` is an appended optional pointer. The
factory validates its permanent INTERNAL range, namespace and exact artifact
inspector identity without reading storage or allocating payloads. A supplied
collector must use the same independently qualified global native writer lease,
native initialization status, artifact controls, digest and PSRAM-only allocator.
No historical legacy owner string is inferred from stored content.

## Deliberately unresolved availability boundary

The current volume API exposes READY, UNAVAILABLE and RETAINED. Its async IO and
BUSY results cannot distinguish physical card absence from transport failures or
USB/session ownership. This increment does not manufacture a no-card witness.

The existing `NativeSettingsLifecycle::hydrate` still returns a boolean and
requires both authoritative Core instances to be ready. Product-visible temporary
RAM defaults, a settings-unavailable state, asynchronous discovery retries and an
explicit late-hydration reconciliation boundary require a separate lifecycle/UI
increment. Such an interface must expose per-namespace availability independently
from Core readiness, leave persistent writes disabled while unresolved, and only
publish newly discovered authoritative settings at an owner boundary with no live
KV grants or drafts. Temporary values must never enter baseline publication.

## First-SD capacity remains unqualified

The production composition must leave `First::Authority` empty until it can
supply real capacity evidence. Neither a held lease, total card sectors nor
native partition space is an SD staging reservation.

The smallest follow-on is a versioned suffix after
`RiscStorageSessionAdmissionV1`, preserving every existing public prefix. A copied
request runs on the selected SD worker within the same exclusive native attempt
and returns verified free-cluster and cluster-size facts bound to media/mount,
scope and operation identity. Admission must conservatively budget cluster-rounded
replica pages, index and recovery staging plus directory/LFN growth, then consume
that result before native arm while the same writer exclusion remains held.

Required touchpoints are the Runtime SDK async adapter/new suffix,
`NativeSdAttempt`/`NativeSdSession` and `CheckedFirstSdCheckpoint`, the X4 SD
provider's `driver.c`, and Drivers' `StorageFatFs/volume.c`/`fs_extensions.inc`.
The pinned FatFs `f_getfree` is not sufficient unchanged: it may trust cached
FSINFO (`FF_FS_NOFSINFO=0`) and its scan loops lack explicit bounded cooperative
checks. The follow-on needs a fresh bounded count with custody/timeout failures
propagated, rather than a synthetic always-successful callback.

## Qualification

The host maintenance fixture executes the actual Runtime graph, CpuPort custody,
source-pinned X4 SD/FatFs stack, checked native store and actual NVS source
collectors. Only hardware sectors, vendor NVS and scheduling are models. Added
coverage includes verified-vacancy fresh initialization, legacy import, checked
SD restore, no-card/busy distinctions, native/SD conflict, corrupt/unknown native
sources, receipt/stage/rename process-death recovery and permanent retained
controls. Existing first-export, lifecycle, cancellation and Files/USB controls
remain in the same suite. Separate fresh and imported recovery campaigns test the
underlying checked protocols across their full interruption matrices.
