# Native worker v5 authority prerequisite

This additive default-off prerequisite is based on the published internal-mapping
prerequisite. It is not native-network activation or a full-owner image.

Ordinary Graph acquisition, including dependencies, refuses ABI5 before callbacks.
The private native seam authenticates the occupied grant, copied policy and full
mapping before committing staged task custody. Existing Runtime and
DeviceProviderExecutorV2 friends can access the private seam; no startup/close
caller is implemented in either. ABI3 and ABI4 remain unsupported.

## Reproduce the bounded hosted cases

This runner is explicitly Darwin-only because the retained strict link recipe
uses 16 Mach-O aliases. It requires existing Clang C/C++ tools, a macOS SDK and
OpenSSL headers/library for the existing hosted loader digest leaf. It installs
nothing. Use unoptimized Python and a new output directory outside the checkout:

```sh
python3 -B test/run_native_worker_v5.py \
  --repo "$PWD" --cc "$(xcrun --find clang)" --cxx "$(xcrun --find clang++)" \
  --sdk "$(xcrun --sdk macosx --show-sdk-path)" \
  --openssl-root "$(brew --prefix openssl@3)" --output /absolute/fresh/output
```

The same seven lifecycle cases and three cohort/import cases run in normal and
UBSan modes (20 total). 58 fresh objects include the unsupported native0/candidate1
Module compile, and eight strict executables are linked. Assertions and current
capacity defaults are checked in every effective preprocessor profile. Exact
source, fixture and consumed-header identities and all command results are saved.

The real Graph, Module and resident worker code is composed with explicitly
modeled ELF mapping, RTOS, physical retirement, export placeholders and the exact
copied SDK pthread unit. The include path preserves the modeled pthread header;
it never silently consumes a restored SDK sibling header. No unresolved-symbol
allowance or production export expansion is introduced.

The private lifecycle003 composition passed 20 cases with guarded mapping
(result 826c4a07d92022df668d36f8fc891c357dabcf8cd0352d02ab1e4a2c270d722c).
The repository-native Darwin entry separately passed all 20 cases in normal and
UBSan modes, with 58 fresh objects and eight strict binaries.
Result: `4a379aa44eb106411746f469f2fd53b95122ce7e7122919ec3b691061f52353a`.
Report: `621c7dc496e90d21377f3ce3b70d8f51a5079d7807130d0d54ec06ed6916f2e0`.
Historical reports remain private; only their identities are recorded here.

After that run, only this documentation, the matching provenance pin, the CI
runner label (`macos-14` to `macos-15`) and source version (`0.4.16` to `0.4.17`)
were updated. Production, test, support, recipe and runner bytes are unchanged.
The dedicated macOS workflow is not yet executed; its tools are resolved from
that runner's existing Xcode and OpenSSL installation without installing packages.

Target build, actual root startup/close, physical retirement, protected internal
memory, cache-off reachability and complete native-owner qualification remain
separate gates. This change does not activate hardware or deploy anything.
