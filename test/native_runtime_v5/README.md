# Native Runtime v5 caller hosted tests

Run from a checkout with the declared existing Darwin tools:

```sh
python3 -B test/run_native_runtime_v5.py \
  --repo "$PWD" --cc "$(xcrun --find clang)" \
  --cxx "$(xcrun --find clang++)" \
  --sdk "$(xcrun --sdk macosx --show-sdk-path)" \
  --openssl-root "$(brew --prefix openssl@3)" \
  --output /private/tmp/native-runtime-v5-new
```

The output must be fresh, outside the repository, canonical (not /tmp or /var
symlink aliases), and short enough for the actual Runtime root-path contract.
No tools are installed. The existing OpenSSL linkage is the ordinary host
digest dependency, not a cryptographic qualification.

The runner compiles the actual Runtime, production DeviceProviderExecutorV2,
Graph, Module, resident worker/task scope, importer and resolver. It executes
the same 34 cases in normal and undefined-behavior-sanitized modes. Assertions
remain enabled. The executable link is strict; exactly 16 preserved Darwin
aliases are explicit test export mechanics, not production symbol policy.
Three disabled/non-native profiles prove new storage and guarded peers absent.

ELF mapping, kernel scheduling, physical retirement, and export placeholders
reuse the pinned support under ../native_worker_v5. Its full inventory and
existing ESP-IDF license/provenance remain required. The small host app and
dependency dylibs use Darwin dynamic lookup only at that explicit fixture
boundary. They are not firmware artifacts.

The guarded test peers observe private records and call the same private
production stage helper during Runtime's actual start-log boundary. They do
not replace Runtime or synthesize a staged owner. Copy/move of ownership is
forbidden. A test-only wrapper around the actual cancellation implementation
counts requests and models immediate scheduling or actual retained-binding
failure; readiness uses the real resident join and safe-tail contracts.
All network I/O leaves hard-fail, and every case requires zero network calls.

Coverage includes eager/cold startup, deferred demand, app-exit denial,
separate stage/map/commit failures, dependent policy/config/code lifetime,
terminal legacy/malformed/unknown boundaries, accepted pending-to-safe tail,
nested close, exact stale/substituted/foreign grants, pre/post-revoke failure,
fatal retained destruction versus clean destruction, ABI2 prefix/extended
descriptor compatibility, and immediate/delayed/failed staged cancellation.
The exact names and compile/link commands are in RECIPE.json.

This is Darwin hosted-model coverage, not target execution, physical retirement,
internal-memory availability, full-owner firmware or deployment readiness.
The independent unchanged ordinary Runtime recipe still stops at its existing
runtime_test.cpp:328 waits-vector assertion on both ec86 and this successor,
after the same six passed groups. That is not a complete ordinary-suite pass.
