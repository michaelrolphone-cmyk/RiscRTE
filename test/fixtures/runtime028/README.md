# Runtime 0.2.28 worker ABI fixture

StorageWorker.h is an exact byte copy of src/runtime/storage/StorageWorker.h at
public Runtime commit 1c1ed8661ea30d47e7e9707c41d492ce3a2b4d70,
Git blob 9aa82a52cd12e28f9f04170f0feba3a49a7fdf71. Existing repository licensing
applies. The test verifies SHA-256 before changing only its namespace in a
temporary file. This preserves the fixed ABI baseline in shallow clones and
source archives; it does not compare against a moving HEAD.

SHA-256: a402ecd14e2c00da069769b6d622e09eb954b5c5bc9d15f10ad0c2a6bdc1d297
