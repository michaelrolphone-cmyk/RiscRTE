# Catalog and atomic settings dispatch isolation

The Waveshare 0.1.41 native source pin
`c258c4ba144247b6eaa4c6f28675ffccec582fe1` assigns both `AppCatalogDriver` and
`AtomicKeyValueDriver` the private dispatch tag `-11`. The selected
`expanded-candidate.ini` profile enables `RISC_RUNTIME_ATOMIC_KEY_VALUE=1` and
`RISC_NATIVE_SETTINGS_LIFECYCLE=1`.

Boot admission correctly grants `app.catalog@1` at instance zero. Acquisition
then matches the earlier atomic-key-value dispatch branch, which looks up an
atomic endpoint at namespace zero. Trusted atomic registration rejects zero,
so this lookup always fails. No catalog table or live grant is published.
The existing portable client consequently reports `acquire`, result zero,
index zero. Those two zeros are diagnostic placeholders, not a successful
snapshot or an empty installed inventory.

## Minimal correction

`AppCatalogDriver` uses the unused private tag `-13`. A compile-time assertion
checks that every selected native dispatch tag is below the unbound sentinel
`-1` and that all tags are distinct. Provider indexes remain nonnegative.
The public capability name, API, C layouts, grant limits, policy, ownership and
storage behavior are unchanged. No data reset, storage migration or native
version/profile change is required by this correction.

## Reproduction and regression

On the unchanged pin, running the existing native catalog fixture with both
feature defines set to one aborts at its first valid `app.catalog` acquisition
(exit 134 on Linux). The old fixture passed because atomic key-value was off;
enabling lifecycle alone does not select that broker.

The catalog runner now executes eight native ownership/inventory scenarios in
each of four feature configurations: neither feature, atomic only, lifecycle
only, and both. Run:

```sh
bash test/run_app_capability_dispatch_tags_test.sh
bash test/run_app_catalog_runtime_test.sh
SANITIZE=1 bash test/run_app_catalog_runtime_test.sh
bash test/run_atomic_key_value_runtime_test.sh
NATIVE_SETTINGS_LIFECYCLE=1 bash test/run_atomic_key_value_runtime_test.sh
SANITIZE=1 bash test/run_atomic_key_value_runtime_test.sh
SANITIZE=1 NATIVE_SETTINGS_LIFECYCLE=1 bash test/run_atomic_key_value_runtime_test.sh
```

The tag test also reintroduces `-11` in a temporary header and requires the
compile-time guard to reject it with the atomic broker enabled, with lifecycle
both off and on.

The atomic runtime fixture also declares and acquires both services in one
invocation, reads a real atomic namespace and the admitted catalog, denies a
second catalog grant, and revokes/reacquires either table without invalidating
the other. These checks are source/host evidence, not hardware qualification.

## T023 integration

When integrating this correction into the T023 lineage, retain or cherry-pick
the unique native dispatch tags, compile-time guard, full feature matrix and
simultaneous-grant regression together. Reconcile against every native tag in
the destination branch; do not restore `-11` for catalog or resolve a conflict
by dropping atomic settings coverage. Rebuild and qualify the actual selected
native feature profile. The correction does not authorize formatting storage,
changing saved settings, altering the ABI, or substituting a product-specific
app inventory.
