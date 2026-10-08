# Product-owned native seed evidence

Some products compose their own early platform startup with an unchanged Runtime
source tree. Their native image can therefore have additional source/option
proofs beyond generic rollback, TLS, IQ and app-policy-row evidence. Generic seed
tools must preserve and verify those proofs; removing them to pass a stock seed
check would lose the product's native custody.

Runtime 0.1.72 supports an explicit trusted Python callback in:

- `provision_seed.candidate(folder, source, extension_validator=None)`
- `provision_seed.compose(..., extension_validator=None)`
- `provision_device.verify_seed(directory, work, extension_validator=None)`
- `provision_device.compose(..., extension_validator=None)`

The product wrapper supplies the function directly and closes over its verified
Runtime and platform source roots. Candidate, seed and owner-profile JSON cannot
select a validator, import code, or supply a callback path. The ordinary CLI keeps
the default `None` and rejects candidates/seeds needing an extension.

## Callback contract

`extension_validator(candidate, blobs)` receives a detached copy of candidate
metadata and frozen core/native bytes plus its declared JSON proof sidecars.
It must recompute its platform startup/options proof from the exact native ELF,
recheck source custody and return exactly:

```python
{
    "id": "product.native-composition-v1",
    "native_proof": additional_recomputed_proof_fields,
    "assets": original_json_sidecar_bytes_by_basename,
    "metadata": additional_candidate_metadata_to_preserve,
}
```

The generic verifier retains all existing native image, source/version/target,
layout, rollback bootloader, TLS, IQ, app-data and policy-row checks. An extension
cannot replace a core proof field or core asset. Its added proof must exactly
complete the candidate's declared native proof; metadata must exactly match
declared candidate fields, with JSON types preserved. Source, version, target,
layout and other core identity fields cannot be supplied as extension metadata.

At most eight additional JSON sidecars are permitted. Each is at most 1 MiB and
their combined size is at most 2 MiB. Names must be safe `.json` basenames outside
the seed's reserved inventory. Byte lengths and SHA-256 must match the candidate,
and the callback must return every declared extra sidecar unchanged. Ordinary
candidate source metadata (`platformio.ini`, partition CSV and requirements) are
not extension payloads. The compact extension receipt is bounded to 128 KiB.

The seed preserves every sidecar byte and an `extension` receipt containing the
validator ID, additional proof, metadata and sidecar hashes. No extension data is
flashed as executable configuration: it is offline custody evidence. At private
first-install composition, the product wrapper supplies the same trusted
validator again. The generic code freezes the seed bytes, checks their inventory
and hashes, reruns native and product verification, and requires an identical
extension receipt before generating private NVS or the initial image.
The private output preserves the candidate and sidecar JSON beside the image,
and carries the verified extension receipt in `first-install.json`. They remain
outside the flash segment map. Reserved private filenames cannot be sidecars.

An absent callback, changed/missing sidecar, altered receipt, overlapping proof,
type-coerced metadata or unknown extra payload fails closed. This API does not
authorize a new native ABI, change pins, add application authority, or replace
full product board/graph/ELF admission. Hardware qualification remains separate.
