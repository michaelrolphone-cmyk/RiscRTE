#pragma once
namespace RiscBoot {class RuntimeFileSources;}
namespace RiscProvision {
struct Profile;
// Read-only policy comparison for an untrusted, next-boot profile. Both roots
// must be independently mounted, immutable stores for the duration of the call.
// This is an additional restriction, not graph/ELF or inventory admission.
// No module is opened, executed, or granted authority here.
//
// Boot and its referenced board must be equivalent JSON, except an existing
// image_source.path may advance between content-addressed filenames for that
// same manifest ID: Apps/<id>/image-<64lowerhex>.elf (or Drivers/<id>/...).
// Source presence, volume_instance, internal_fallback and all other fields
// remain exact. Path advancement requires internal_fallback=true on both sides.
// This does not authorize a new media selection or fallback.
// Every selected app and provider retains all manifest fields except a
// strictly newer version.
// An equal version requires an entirely equivalent manifest. Cohort identity,
// layout and native firmware remain exact; only a newer product version may
// also change source_revision. Canonical numeric versions cannot downgrade.
// JSON policy numbers must be integers; object key order is immaterial, array
// order and JSON types are preserved. Cosmetic fields are preserved as well.
//
// closeRetained is sticky. A prior or newly failed close rejects the operation;
// callers must retain the mount/owner and must not retry an uncertain close.
bool preservingPolicy(const RiscBoot::RuntimeFileSources* sources,const char* installedRoot,const char* candidateRoot,
                      bool& closeRetained);
// Bind every explicitly mirrored (internal_fallback=true) selected SD image to
// the SHA256 of its exact internal fallback in the pinned profile inventory.
// This verifies metadata only, with no SD I/O or image execution. The caller
// must still verify inventory bytes and perform full native candidate admission.
// Unchanged legacy sources without internal fallback are not mirrored here.
bool preservingImageSources(const RiscBoot::RuntimeFileSources* sources,const char* candidateRoot,const Profile&,
                            bool& closeRetained);
}
