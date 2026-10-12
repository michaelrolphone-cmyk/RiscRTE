# X4 native reader checkpoint

This working checkpoint starts from Runtime `3b90cd11cf0ff4e8116bb54d2d9eb9c7c67a148b` (version `0.2.22`, tree `eb11c705a9594ccb1e86d30048e25df95526e8fd`). It carries only two production reader changes qualified in Watch source `267aab6d9382ce4921a1536fab1620749b2f5301`: regular-file ELF size through `fstat()` with the existing seek fallback, and bounded 32 KiB/two-millisecond metadata scheduler checkpoints.

`src/bootstrap/Json.cpp` and `lib/elf_loader/src/esp_elf.c` are byte-identical to those two reference files. No other production source, SDK ABI, native import authority, graph admission, application/provider policy, cache selection or lifecycle code is changed. The source version advances to `0.2.24`. All 93 live branch refs (91 distinct heads) and eight live tags were checked on 2026-10-12: the highest source version was `0.2.23`. The Watch owner confirmed no later reservation; `0.2.24` is reserved for this X4 port. No tag, release, merge or device operation is performed.

The X4 composition uses platform source `489485d9ca597e88aa94e0379c6f1d860a3e5c47` and the previously delivered native options: `esp32s3-16mb-appdata-iq-stage`, 24 app policy and requirement rows, 64 installed-app slots, 40 providers, 512 retained-wake bytes, USB PHY enabled, shared-files transport enabled, app image cache disabled. The prior delivered native composition used the same Runtime base and platform `a61836c8b3eae74284f244e130cee0ff9c2749e8`; exact source custody is recorded by the X4 composer. No Watch profile is used.

See `ELF_SIZE_QUERY.md` and `METADATA_READ_CHECKPOINTS.md` for algorithm and boundary tests. Their Watch measurements remain reference evidence, not X4 timing claims. X4 host evidence must use the exact delivered X4 metadata store. Build and host tests do not establish physical wake timing or device qualification.

The cache-pressure regression runner additionally links its existing production ELF validator, resolving a pre-existing missing-symbol link dependency. This is test wiring only.
