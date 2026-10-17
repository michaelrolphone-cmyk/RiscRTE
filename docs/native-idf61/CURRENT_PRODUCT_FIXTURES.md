# Current native product matrix

The native-idf6 workflow uses qualifier-only successors of the historical product
fixtures. Baseline factory/SD hashes, SDK, System and Drivers pins are unchanged.
This is current-source regression qualification, not a new release or hardware
claim. Historical Runtime 0.4.0 release evidence and original product commits are
retained unchanged.

| Product | Historical fixture | Current qualifier |
| --- | --- | --- |
| T-Watch | 74f7491dc470b92dfa41fce41bf0c500f3714a94 | 364bdd9ba58edd3f6fe1dc16d361db53d6de81af |
| Waveshare | 25c7151a1ac47e1a19a55b93c4cc3eaf2d4a4298 | 86876d56d5b60cce494c922916e8f22fbc553f2a |
| X4 | a9009a5d774ae7060b9f298fdcbc81be6c22cc17 | 41b8efb25a4cb4d3ab9cfac50ac625c11fab43a1 |

The host fixture binds both metadata roots explicitly and reads each cohort ELF
through the candidate Runtime image source. Production admission, role/import
checks and zero hardware/product-storage callbacks remain mandatory. X4 requires
a unique Runtime version in ELF and BIN matching the locked Runtime source,
rather than an obsolete literal 0.4.0. Its product version and startup proof stay
strict. Normal and sanitized cohort/SD admission, fresh same-process build
custody, final linked IQ placement and immutable baseline payloads remain gates.

The narrowly named fixture branch runs the same complete matrix before source
integration. A green host/target run never qualifies physical hardware.
