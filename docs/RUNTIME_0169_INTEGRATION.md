# Runtime 0.1.69 integration

This local integration preserves all three independent completed histories:

- Stream sessions 0.1.66: `ace13cd69f432a6ca7d616f4e3a1607e730ef83e`.
- Cache pressure 0.1.67: `7f37e876ac1a7e6af2eb94e8cbef0deadf536454`.
- Policy metadata option 0.1.68: `4cad37a3e23c9815da17eb44abd2c95a35bf84e2`.

Their common source is `2dea92258b1e3e1d2592ff7064d7cc74d28a07ee`.
The merge commits preserve authorship and the original feature checkpoints.
The 0.1.69 allocation was checked against current remote tags, branches and open
PR version claims on 2026-10-08. It changes no released or frozen product.

## Conflict resolutions

The native app-ledger allocation now uses the cache-pressure wrapper's single
retry. A final allocation failure ends the newly started stream invocation before
returning the existing app-allocation error. The superseded direct cache destroy
and second ledger retry are removed. Both public Runtime method declarations
remain present. The Runtime ABI keeps the appended stream-client suffix.

Host test link lines retain the new stream implementation and all policy-bound
matrix flags and allocation tracking. The cache-pressure fixture links the same
production broker/queue implementation as Runtime. The additional 17-row runs
exercise the combined metadata shape, without raising live grant or manifest
requirement bounds.

A production fixture reproduced an inherited descriptor-boundary defect: a
recognized stream tag with a truncated version was accepted as a legacy
provider. Admission now reads the tag when that field alone is complete, and
rejects a recognized descriptor shorter than its complete tagged layout before
reading the version or pointer. Tag-only, partial-version and partial-pointer
fixtures must reject before provider start; unrelated tags and legacy prefixes
retain their existing behavior.

## Selection and boundaries

`RISC_APP_IMAGE_CACHE` defaults to zero; opt-in remains `=1` with the documented
allocation-pressure limitations. `RISC_APP_POLICY_ROWS` defaults to 16; only an
explicit 17 selection enlarges policy metadata. Live grants and manifest
requirements remain 16. No new physical transport, app, provider, capability
policy, UI or product profile is installed by this integration.

Run the focused stream, cache-pressure, multi-namespace, demand-retention and
retained-wake tests after any conflict resolution. The serial software witness
requires `SERIAL_SYSTEM_SOURCE` pointing to the exact qualified client checkout.
It is not a USB/UART implementation or physical qualification.

Native builds remain single-job and must use the exact committed source. Host
and target passes do not establish complete-product memory headroom or hardware
behavior. Verification receipts are stored with the resulting candidate, not
inferred from the earlier independent results.
