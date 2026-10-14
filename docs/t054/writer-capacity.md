# T054 exact-writer capacity checkpoint

NativeSettingsLifecycle and Assembly accept an explicitly configured bounded writer capacity (default 6, maximum 32). Runtime atomic binding capacity is independently configurable (default 8, maximum 64) to account for writer rows plus namespace read-through bindings. All IDs, duplicates, namespace and ownership checks remain exact; no wildcard authority was introduced. No product selection is changed here.

The real X4 provider/Runtime assembly fixture passes seven ASan/UBSan scenarios at 32 writers: maximum/six writers, capacity overflow, duplicate ID, invalid ID, wrong namespace, and checked fresh absent-card fallback. Permanent factory INTERNAL control is 16,952 bytes in this host fixture; target memory admission remains required. LeakSanitizer is disabled for this deliberately permanent-allocation fixture, as recorded in the provenance. Address and undefined-behavior checks remain enabled. No hardware qualification is claimed.
