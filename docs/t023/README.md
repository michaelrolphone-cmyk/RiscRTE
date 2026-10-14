# X4 internal package integration checkpoint

Exact cumulative Runtime f734d91780cdb4f1ba36fe82dcbd8fa28f25bb2b lineage. Port the qualified opt-in internal package volume/media and heap workspace changes from Watch cbdbeb1/ee54979. Preserve X4 NativeSettingsLifecycle continuation fences and the existing native capacity receipt suffix; the package exit fence composes with them. No formatter or implicit grant is introduced.

Normal and Linux ASan/UBSan removable runtime/package-file tests pass, including internal media, absent SD, stale handles, interrupted replacement, retained close, authority and loader validation. Existing524104 native capacity geometries pass. Target product build and complete admission remain pending; no hardware test or flash.
