# Bounded canonical-image envelopes (integration candidate)
The raw LZ4 decoder is in `src/runtime/storage/Lz4Block.h`; the fixed envelope
parser is `ImageEnvelope.h`. `RuntimeFileImage.h` owns the source handle and
single caller-owned logical allocation. This branch implements internal
ordinary-app reads and native bank admission; it is not a deployment receipt.

## Representation and authority
The 32-byte little-endian header is magic `RTEIMG1\0` (8 bytes), header size32,
codec1, decoded size, payload size, decoded CRC32, reserved0 (six u32 fields).
Physical EOF must equal32+payload; decoded size is at least52 and bounded by
the caller's existing image limit. CRC32 detects corruption, not authenticity.
Store inventory SHA and expected lengths describe physical bytes. Existing ELF
validation/admission receives canonical decoded bytes and size. These authorities
are never substituted for each other.

The reader's envelope option defaults false. Runtime enables it only for its
default or admitted application policies, excluding any image-source policy and
every path also used by a driver. Native provider policies and removable/package
images remain raw-only. No mirror, SD route, provider digest or saved data is
rewritten. Cache keys retain the physical source path; cached content is canonical,
and existing immutable-session invalidation and custody fences remain in force.

## Bounded decoding and custody
Input reads are at most128 bytes and output checkpoints at most256 produced
bytes apart. Output itself is the match dictionary; no heap or64KiB history is
used by the decoder. The reader allocates one decoded image, never a whole
compressed image as well. Short reads are supported. Truncation, overflowing
lengths, zero/out-of-history distances, wrong final size, trailing bytes,
CRC mismatch and inconsistent same-open EOF/position fail closed.
Every callback is fenced for source/owner safety. Retained, unknown or lost
custody stops further I/O and cleanup; clean failure uses checked close/free.
Physical observation includes the header and payload and is separate from
decoded CRC. Input and output work both advance the existing checkpoint budget.

## Qualification boundaries
The owning workflow runs the actual decoder and RuntimeFileImage normal and
ASan/UBSan suites, independent pinned python-lz4 and owning System encoder
corpora, unchanged raw regression, actual Runtime/cache raw and encoded modes,
and target compiler object/stack measurement. NativeBank's separate owning
workflow covers provisioning/admission and custody. Host/target compilation is
not physical-device execution. A passed decoder or reader test alone does not
qualify a complete product update.

An old raw-only Runtime must reject an encoded cohort. The preserving upgrade
route requires a higher-version decoder firmware-only update first, cloning the
original raw store unchanged, followed by reboot/confirmation, then admission of
the encoded final cohort by the new Runtime. Both phases retain existing paired
bank records, hashes, boot selection and rollback behavior. No repartition,
factory erase, unpaired replacement or direct old-Runtime encoded admission is
authorized by this format. Product-specific upgrade qualification is separate.
