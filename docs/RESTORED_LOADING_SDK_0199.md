# Restore original resident-loading SDK source compatibility

This is a source-only repair over the frozen native source archive delivered in
X4 0.1.49: original revision `11b16cf60c5d3bd9b6a202f25b7b86a2ff05ca21`,
tree `fc135aef0b12314df77a793ab6f94ca95b84ed02`. The local archive checkpoint
`545cc8b98d264fe45f60e0aeabadd490ea450eb5` has exactly that tree; it does not
claim the original commit identity or a successful remote publication.

`RiscResidentShellV1.h` and the original loading documentation, ABI-prefix test,
runner and its supporting fixtures/runners are restored byte-for-byte from
Runtime 0.1.98 revision `da3fa1a3ab177bc96af61298849a40ea9ca314f3`, tree
`21781fca6052a441dff599e26ac0e4fa8c79a155`. This restores `stddef.h`,
`RISC_RESIDENT_CALLBACKS_V1_SIZE` and
`RISC_RESIDENT_CALLBACKS_LOADING_V1_SIZE`. The callback fields and their binary
layout are unchanged. The richer original loading suite replaces the smaller
reconstructed loading checks and also covers ordinary flag-off behavior.

All Runtime implementation `.cpp`/`.inc` files, SDMMC sources, CPU hardware and
GPIO interfaces, platform configuration and native version remain unchanged.
No panel/provider source or binary is included in this repair. The delivered
0.1.49 native firmware and full image remain frozen.

## Verification

- Original unmodified ABI-prefix test compiles against the repaired header with
  host GCC and pinned Xtensa GCC 8.4. It fails against the delivered archive's
  unmodified header. No compatibility include or macro injection is required.
- A callback-layout probe produces byte-identical old/repaired object files on
  host and Xtensa, including all field offsets, struct size and function-pointer
  extent.
- `bash test/run_resident_loading_test.sh`: 32 original loading cases pass.
- `RESIDENT_NATIVE_MEMORY=1 SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash
  test/run_resident_loading_test.sh`: the same 32 cases pass with production
  native memory accounting and ASan/UBSan.
- Ordinary resident shell and legacy suites pass with native memory accounting
  and ASan/UBSan. Native SDMMC and CPU-port suites pass normally and sanitized.

These checks establish software/SDK compatibility. They do not qualify physical
sleep, storage performance, panel timing or an assembled new product image.
