# Native worker v5 target prerequisite

This additive job builds the generic ESP32-S3 runtime with the existing official
ESP-IDF 6.1 image and only RISC_CPU_SCOPE_V5_CANDIDATE=ON added to the generic
recipe. Existing workflows and default firmware options are unchanged.

## Reproduce

Use the pinned SDK commit fff9895c82d744c7237be8847347bdd1b07c6643 and its official
GCC 15.2.0 release esp-15.2.0_20251204. Activate the SDK normally. Build into a fresh
directory outside a clean source checkout, with one compiler job:

~~~sh
source_sha="$(git -C "$source" rev-parse HEAD)"
idf.py -C "$source/idf" -B "$build" -D "RISC_SOURCE_SHA=$source_sha" \
  -D "SDKCONFIG=$build/sdkconfig" -D CCACHE_ENABLE=0 \
  -D RISC_CPU_SCOPE_V5_CANDIDATE=ON reconfigure
set -o pipefail
cmake --build "$build" -- -j1 2>&1 | tee "$build_log"
python "$gate_source/test/test_native_worker_v5_target.py"
python "$gate_source/scripts/native_worker_v5_target.py" \
  --source-root "$source" --source-sha "$source_sha" --sdk-root "$IDF_PATH" \
  --build-dir "$build" --build-log "$build_log" \
  --cc "$(command -v xtensa-esp32s3-elf-gcc)" --output "$audit_json"
~~~

gate_source may be a newer checkout containing the audit. The explicit source
root/SHA always identify the code actually built, not the audit's own revision.
The workflow instead builds and audits its exact GITHUB_SHA from a fresh checkout.

## What the gate establishes

- Clean exact source/SDK identities, generated build identity and pinned compiler
  release, with observed host-specific compiler/tool hashes.
- Actual six candidate and three Runtime/Graph/Module object files, completed
  Ninja/build execution records, and byte-identical members in owning archives.
  A compile database entry alone is not accepted as proof of compilation.
- Named worker/binding/retirement functions retained in the final Xtensa ELF,
  resident pthread destructor resolution, and retained Runtime start/close code.
  Objects compiled but later discarded are reported separately; no fixed total
  global-symbol count is required.
- Actual generated target, memory-protection/lock, PSRAM and TLS external-allocation
  configuration, with no product policy or internal-placement selection.
- Static allocated sections/load segments and agreement with the owning build's
  image/partition size check. Overlapping dummy load segments are not summed as
  RAM usage. Debug ELF size is not flash/RAM use; runtime heap/stack peaks are not
  measured.

The unit fixtures are synthetic positive/negative parser and contract tests, not
firmware execution or hardware evidence.

## Retained local target evidence

The original macOS target build used source
83075d6f97cf352ef9b2137e46df55f9e58fe826, tree
77707373cbd516e30424007207fe956faa579734 and the SDK/compiler pins above.
Its build report SHA256 is
89d21b04b6a94d80004723c09b0c3ff2bb596f0cede5c0c64c3071f2270c568d;
the separate saved-output audit report is
6919a83e3377f1b506547997fa75af9a561c690cb5b29ede23cb7947e0788460.
The final ELF SHA256 is
9654aa017bce44a0d7e31393ead16d722640cea76235cc2333f96ae126ad5a24.
All nine selected TUs compiled and were archived; 51 candidate global definitions
were retained, while three helpers were discarded. The image is 1,294,848 bytes,
fitting the 3,145,728-byte app partition with 1,850,880 bytes free.

The repository-native audit passed against that exact saved build, and all 23
unit methods passed. Combined report SHA256:
f3e6ba0b55ceceef724cc04cfb003fddcc23894d28821f3688aeb547f0337454;
repository-native audit SHA256:
32a3ffb7dcc5ae036c33d1b95c711d0468a38ca160f5c2ced6279117fad55d41.
This paragraph is a post-test documentary update; tested script/fixture bytes
are unchanged. The earlier audit attempt rejected the SDK's generated response
files; it is preserved as harness evidence, not a target regression. The corrected
gate authenticates and expands their actual simple option format before checking
selection. Recorded project/SDK/build/compiler roots, source operands and include
roots are checked as well. The new Linux container workflow is separately pending
CI; the macOS result does not claim it passed.

## Limits

Candidate compilation is not native-owner activation. No selected product root,
shared-app execution, task retirement on hardware, protection change, device
connection or deployment is performed. The existing internal adapter remains
unavailable (ENOTSUP), and internal placement remains default-off. Immutable-code
provisioning/admission/lifetime support is still an architectural prerequisite.
No TLS memory-placement requirement is relaxed. Full raw private receipts and
local firmware stay private; CI retains its own bounded build/audit metadata.
