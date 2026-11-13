# Product-selected AppData mount geometry

The separately provisioned AppData partition and its mounted LittleFS superblock
must describe the same byte capacity. The native mount fence now derives the
expected block count from `RiscNativeAppDataLayout::Bytes / 4096`, retaining the
qualified 4096-byte erase block and all existing VFS/configuration/custody checks.
It does not special-case board names or accept arbitrary mounted capacities.

The previous fence required 128 blocks even when a product explicitly selected
a 1 MiB partition and packaged a valid 256-block image. LittleFS could read that
image, but registration was rejected by the owning fence. Standalone startup then
returned before manifest preparation or display-provider entry. No device hang
or display fault is needed to explain that boot stop.

Stage-enabled builds report observed/expected partition geometry, filesystem
block count, mount result and bounded admission/custody/fence failure reasons.
Formatting and growing on mount remain disabled. A smaller valid filesystem in
a larger selected partition is rejected, not grown. Creating the already-owned
private settings directory after successful mount remains existing behavior;
those normal filesystem writes are not formatting.

## Regression

Run the actual pinned LittleFS VFS and native owning fence:

```
python test/run_native_app_data_geometry_test.py --output /tmp/appdata-normal
python test/run_native_app_data_geometry_test.py --sanitize --output /tmp/appdata-sanitized
```

Add `--appdata-image /path/to/appdata.bin` to load an existing 1 MiB host image
for the Badge cases. The input file is read only and copied into host memory.
No device, remote, firmware flash or image publication is performed by the test.

The 512 KiB default and 1 MiB product run the complete owning-mount suite:
healthy custody, failed writes and closes, lost custody, backend operations,
restart recovery, package power cuts, and exact/undersized on-disk geometry.
Additional 64 KiB and 2 MiB products run exact/undersized geometry cases. Every
undersized filesystem must fail registration with zero program/erase bytes.
Diagnostics are enabled in this matrix so their compiled formatting and output
are covered. ASan and UBSan are enabled by `--sanitize`; leak checking is disabled
because intentionally retained resources live until simulated cold power loss.
These are host-model results, not a physical hardware qualification.
