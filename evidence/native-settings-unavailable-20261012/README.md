# Availability/source sweep receipts

Large manifests and target-command records are stored as deterministic lossless
gzip. compression.json maps original paths and exact decompressed byte hashes.
Earlier summary hashes refer to those original bytes. Source qualification
remains tied to the exact commits in each summary; no hardware claim is made.
