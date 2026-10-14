# T051 Waveshare native settings candidate — not deployable

This candidate merges shared native settings service 1790560c174809f8e8f8d10f2e1e121d6f4c1485 onto delivered Waveshare runtime d493ca3ede561d410a858440ee2db9073c963426. Conflicts preserve the vendor display route, FileSource, Watch runtime version and lifecycle fences. Explicit profile macros select logical owner, volume instance, writers and namespace mask; defaults preserve the X4 profile. Namespace mask 2 prevents loading, binding, discovering or checkpointing namespace 1, retaining existing product Settings storage.

The Waveshare target with namespace 6 selected builds successfully (single-job Espressif build). No deployed persistence qualification or complete-store qualification is claimed. Do not publish a firmware using this candidate yet.

Exact blockers discovered through production source tracing:

* Waveshare boot lacks the installation-volume declaration required by NativeRuntimeStoragePin; the intended existing SD instance is 12.
* Its deployed sdmmc-fatfs 0.1.0 provider is synchronous and lacks the storage asynchronous/package suffix required by NativeSdAttempt. A boot declaration alone cannot make this candidate work.
* Namespace selection requires full lifecycle and unrelated-settings regression coverage before deployment.

The full-feature shared Wi-Fi controller compiles with shared keyboard, atomic namespace 6 and availability selected, but remains an unqualified Waveshare candidate. Existing product/native images and all SD contents remain unchanged.
