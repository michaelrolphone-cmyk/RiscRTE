# Badge Runtime object recovery

This pack restores the original Runtime commit
`046f4c22b263739f3bf3f96f888617d72eb3d498` and its consumed SDK anchors.
The published snapshot `07e982c27b9e584fcba6b49d1c84cacaa558d24f`
has the same tree `876d77de61b53c05343a1de754c32eb476c3ed3b`, with
different ancestry. Neither identity is substituted for the other.

The original objects came from these verified, preserved recovery inputs:

| Input | SHA256 |
| --- | --- |
| badge-source-recovery-9bf67c4.zip | 96b11b415c9334a4563f98e5ea70f90761af26218b9d7e58e5c0f394dd2681f7 |
| badge-shared-clock-c746d7c-source-target-evidence.zip | 5b50569951f9986990cdb169c00508c7edb2acb146a9bbe2f88e3a102f252660 |
| badge-performance-followup-740e420-evidence.zip | 999b37b2d2f6e656749404b91c5ccd32b0aac91fb3dedce5903eb7adb55a9e9e |
| badge-time-sync-21b9d14-source-evidence.zip | 86c20a529c6a40e88b7dca664b70df42e9c1b4e45c6c6cfc8799655e9eccc4df |

The standalone snapshot supplied baseline and canonical SDK objects. The
shared-clock bundle restored `37fca3e2244bf71ac64bd8cd7e62eb94f4c09a9d`.
The isolated native-time patch identifies parent
`273bdbccf65647a803ab0586a418f8283732baec`, author/committer
`Codex <codex@openai.com>`, timestamp `2026-10-26T05:37:22-06:00`, and
message `Use exact bounded 32-bit arithmetic for native millisecond conversion`.
Applying it to that parent produces tree
`654573886d790f9b0b15a6f4b7c4cd2d884b578b`. Reconstructing the commit with
those fields yields exactly `740e420d98943dc321d984bc5535edeb5bfb5cfb`.
This exact hash match was required before importing the successor Runtime
bundle; a candidate with any different hash is not original source evidence.

The successor bundle verified its prerequisite and restored `046f4c2` with
the expected tree. Git connectivity checks passed. The manifest records
4,200 original objects and four explicit shallow boundaries; unavailable
ancestors are not synthesized. Clean empty-repository pack import, tree and
connectivity verification pass. These checks establish source recovery only,
not a target build, application integration, hosted CI, or physical acceptance.

Drivers owns the consumer and its integrity/mutation tests. Consumers must pin
this publication commit, verify each part, the combined pack, object count,
source identity/tree and shallow boundaries, and retain the original SDK pins.
