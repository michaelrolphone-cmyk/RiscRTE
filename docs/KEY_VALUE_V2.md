# Explicit larger key-value records

Firmware 0.1.14 adds `storage.key-value@2` and the provider-only
`storage.key-value.bound@2`. The public v2 headers alias the unchanged v1
function-table layouts and retain the same key grammar, status codes, explicit
namespaces, generation/revocation and uncertain-write behavior. Only explicitly
admitted v2 calls accept values from 1 to 2,048 bytes. V1 calls still reject writes
larger than 64 bytes and fail closed when a backend reports an oversized read.

The internal backend declares `maxBlobSize`, default 64. Admission rejects v2
before module loading unless the backend explicitly supports at least 2,048 bytes.
The existing NVS adapter supports the larger bound; it still commits a single
key and verifies exact bytes using a separate read-only handle. There is no
formatting, erase, implicit retry, cross-key transaction, schema or migration
policy. IO can mean the value persisted. Callers must preserve the intended
bytes for retry and reject malformed or incomplete records; no hardware
power-loss atomicity or flash durability qualification is claimed.

An application can separately declare KV v1 and v2 requirements, with exact
version/namespace grants. Same-version duplicate requirements/grants remain
invalid. Other capabilities retain their single requirement rule. The provider
map still admits at most eight exact keys and preserves read-only entries;
there are no wildcard keys, namespace aliases or new grant-count allowances.
Both versions deny stale callbacks, wrong-owner calls and revoked lifetimes.

Read scratch buffers remain locally owned and are wiped on every normal exit.
The bound is 2 KiB, not a general file API. The native port already uses a 16 KiB
loop stack. The v2 change adds a maximum 4 KiB of simultaneously nested broker/NVS
read scratch, or 4 KiB of NVS put/readback scratch. Consumers must include their own
frames in stack-budget analysis; this is not permission for unbounded records.

`test/run_key_value_v2_test.sh` loads a real dynamic app and ordinary provider
through Runtime/Graph. It covers explicit backend admission, mixed v1/v2 grants,
1/64/65/160/1296/2048-byte values,2049-byte rejection, namespace isolation,
read-only/unknown-key denial, probes/short buffers, faulty partial and oversized
reads, uncertain persisted writes, wrong-owner calls and stale revocation.
The existing v1 app/provider and native NVS fault fixtures run alongside it,
including normal and ASan/UBSan builds. Hosted target builds remain required;
these tests do not qualify real flash or device behavior.
