# Infrastructure v1.2 startup investigation

The owner's 2026-10-30 v1.2 attempt reached `RTE_APP phase=entry` but
produced no agent status lines, and the owner confirmed that no
`RiscRTE-Setup` network appeared. This is a hardware startup failure,
not a successful captive-portal qualification. The cause is not yet isolated.

The captured flash session wrote the complete 16 MiB payload, then lost USB
during the MD5 verification command. Transfer completion is not verification.
The subsequent monitor observed metadata admission and application entry, with
no reported application return or portal-ready state through its captured tail.

The v1.2 native binary prints source `921a3d1e2f5b0224bc9175d3b8eae3e2bbcda968`
although the source manifest correctly records the portal implementation at
`cc8235aea70cfa776e88ba11c930185450e5457c`. CMake had retained the identity
from its earlier configuration. The old marker does not mean the owner selected
an old binary. Release verification now rejects an embedded/manifest mismatch,
and CMake watches Git HEAD and its symbolic ref for incremental builds.

The follow-up product profile enables existing stage diagnostics and the
application reports runtime access, each grant acquisition, memory sampling and
the first bootstrap call. No credential values are added to diagnostics.
These changes expose the failing boundary; they are not a claim that the
unidentified hardware startup failure is fixed. Provider implementation and
pairing pins remain unchanged. The binary still uses on-device captive-portal
setup and requires no workstation credential injection.

Hardware acceptance remains: portal SSID appears, phone opens 192.168.4.1,
settings commit, station joins, reboot preserves settings and live service
reconciliation completes. Preserve these distinctions in release metadata.
