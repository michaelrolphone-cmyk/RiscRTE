# Complete-path receipt storage

Large generated manifests are losslessly stored as `.json.gz`. Prior frozen
summary paths and hashes identify the original decompressed bytes, preserved
exactly. `manifest.json` maps logical to stored paths and verifies both hashes.
This changes only evidence packaging, not executable source or qualification.
