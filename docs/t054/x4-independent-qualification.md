# Independent X4 persistence qualification

The current-input runner records the actual graph hashes, fallback selection, and source provenance rather than claiming its legacy frozen .99 graph. Exported inputs can be checked against an explicit per-file commit/SHA256 manifest; ordinary Git input validation remains supported.

Published provider source 3c32f85de9c3c80a6b085753d16744c47712172d and matching FatFs source 11bc45d263f830c70cea53a8c8749e4fcf17b041 resolve the unavailable historical provider pin and stale SDK mismatch. Reader fixture inputs are 45cf61ac013fb618e8d7fed63217a35350484d52. The attached manifest records exact files and commands.

Sixteen scenarios pass ASan/UBSan: fresh absent/busy fallback, legacy absent/conflict/corrupt refusal, bare initialization and native error refusal, valid fresh SD restore, separate-process offline Save/reload, returning vacant/conflicting card, first-publication success/conflict, foreign media, and USB exclusion. This exercises the source-pinned X4 SD/FatFs provider, Runtime graph and custody with simulated hardware/NVS. It does not reproduce the deployed SD ELF or qualify physical hardware.
