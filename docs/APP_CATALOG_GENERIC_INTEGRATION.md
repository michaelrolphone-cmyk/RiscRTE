# Catalog integration on the generic transport lineage

This local integration applies the published catalog diff from
`06da76aa7f074220b74a282badca0ed0061f1643` to
`a6c7f273025a59b1383e9c2795fa0ff0a8e9efad` onto the generic Runtime checkpoint
`815013e4b3c623e5b9b788a0062d8b5e864e935c`.

The only patch-context adjustment removes the absent FileSourceRuntime.inc
context line. AppCatalogRuntime.inc is inserted immediately before the existing
FileOpenRuntime.inc. No source from the recovered native lineage replaces the
generic SPI/display backend, GPIO recovery, startup, partition profiles, or test
fixture repairs. The published catalog implementation and its tests are otherwise
unchanged. Shared UI consumers are not enabled by this Runtime commit.

Local macOS qualification runs the eight production Runtime catalog scenarios
normally and under UBSan. GNU --wrap is unavailable on macOS, so the temporary
runner redirects the same three direct filesystem calls at compile time and
links separate, unredirected libc forwarding functions. The zero-I/O assertions
remain active. Temporary runner adjustments also accommodate Bash3.2 empty
arrays and existing unrelated Clang unused-variable/field warnings. These are
host checks; no target instructions or hardware are exercised by them.

The existing 21-app/28-provider candidate still passes real Runtime admission
and native ELF/import checks with all49 ELFs, zero hardware calls and zero storage
calls. All eight startup variants pass. Native target compilation is performed
separately after this commit so its build identity refers to committed source.
Linux ASan and complete Runtime CI on this integration are not yet claimed.

No hardware access, flashing, GitHub publication, or Drive upload was performed.
