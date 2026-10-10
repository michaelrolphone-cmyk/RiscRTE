# Canvas cohort capacity

Runtime 0.2.4 admits up to 28 installed applications in PSRAM cohort profiles,
so the Watch can retain all 24 existing apps and add Model Viewer. Legacy
static profiles remain 19 apps, 17 providers and 32 graph grants. Cohort
provider capacity stays 28, graph grants stay 44, and per-invocation live grants
stay 16. This adds no capability or implicit privilege.

The `esp32s3-16mb-appdata-iq-policy24` target preserves the paired AppData
partition map and IQ reservation, with 24 immutable policy/requirement rows.
Use the product's exact full-cohort admission against the built native ELF,
not a metadata-only fixture, to qualify an install. The bound test exercises
legacy, paired and explicit PSRAM selections. Hardware remains untested.
