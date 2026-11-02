# Standalone bounded raw LZ4 decoder (not deployed)
Source: src/runtime/storage/Lz4Block.h. No RuntimeFileImage, store, manifest or
loader selection is changed by this branch.

The caller supplies exact compressed length, exact logical output length and
output capacity separately. A read callback may return short reads; zero before
the declared end is an I/O error. Reads never exceed 128 bytes. One raw LZ4 block
is decoded directly into caller-owned output, including overlapping matches.
The existing output is the match dictionary; no heap or 64 KiB history exists.
Output checkpoints are at most 256 produced bytes apart. Every callback is
preceded/followed by a safety check. Lost safety, explicit RETAINED and unknown
callback results stop all subsequent callbacks; failures never free or close.

Success requires all declared compressed bytes consumed, a final literal
sequence, and exactly the expected output bytes. Token lengths are bounded by
remaining output before addition; zero/out-of-history distances and truncation
fail. Any partial output on failure is invalid and must never be loaded.
A caller must independently validate envelope size/hash and physical EOF; this
decoder deliberately never reads beyond the declared compressed block.

## Future integration seam, not implemented
RuntimeFileImage should own the existing file handle and output allocation.
Its adapter should use RuntimeFileView reads, preserve current owner/safety and
retained fences, and translate retained/uncertain cleanup to terminal custody.
The envelope must bound logical length by the existing image cap before allocation.
The loader must receive only exact canonical ELF bytes after logical digest and
structural validation. No alternate source, retry, close or free after retained.
Normal clean decode failure permits only the existing checked cleanup path.
A physical envelope digest/length and logical ELF digest/length are distinct;
neither can silently replace the other's inventory/admission meaning.
This work does not yet define or publish an envelope or modify admission.

## Qualification
test/run_lz4_block_test.py uses pinned independent python-lz4 4.3.3 raw block
compression for deterministic binary, repeated, random and million-byte inputs.
The C++ test runs short-read fragmentation, exact byte comparisons, invalid
bounds, truncation, overlap, malformed streams, and every read/checkpoint failure
boundary including ordinary-success context loss. The owning workflow runs
normal and ASan/UBSan, then exact Xtensa GCC15 compilation with stack/object and
undefined-symbol receipts. Target compilation is not physical execution.
