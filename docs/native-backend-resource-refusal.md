# Native backend resource refusal qualification

These are host negative campaigns on the actual checked NativeSettingsBackend,
NativeAppDataMount, payload adapter, AppDataFiles and pinned LittleFS implementation.
They do not admit an X4 firmware/app cohort or establish physical heap capacity.

The full-space case imports both legacy namespaces, keeps an ordinary AppData peer,
and fills only /budget-fixture/filler-* files until the real mounted partition has
less than the existing 32768-byte recovery reserve. Both namespaces must reject a
replacement with NoSpace, preserving the entire fixture flash image, NVS records
and Core revision. Recreated Core/store views reload the selected values. Only
fixture-owned filler names are removed before successful retries; peer and original
legacy values are checked again. This is object reload, not a physical reboot proof.

The payload case refuses the first actual PSRAM payload allocation in each
namespace's mutation. It requires NoMemory, one request without fallback,
unchanged flash/NVS/revision and a later successful retry. Existing adapter and
scratch tests separately exercise range/alignment/overflow, wipe-before-free and
multiple client refusal paths. Later allocation ordinal fault coverage is not implied.

The shared backend runner accepts a checked-in test filename and explicit case array.
Its legacy defaults stay unchanged on this branch. The same generic entry point is
also used by independent LocalOnly qualification; combining branches must retain the
other branch's default cases without changing production policy.

Host fixture SDK inputs are X4 c8004c5c8362b142ebb35a0e1c5d266532a11581 and
T5S3-Reader 45cf61ac013fb618e8d7fed63217a35350484d52. The runner obtains its
LittleFS inputs through the existing exact-profile checker. These are test helper
pins, not a claim that an old X4 profile is the selected new firmware.

Requested-payload accounting excludes vendor/STL allocations, allocator headers,
fragmentation and actual available heap. Operation-time free-space refusal does not
reserve replacement space against subsequent ordinary writers or across reboot.
A durable shared-AppData budget, whole owner stack bound and final native/app/provider
cohort admission remain separate unresolved gates. No IQ placement, partition,
production limit, saved-data route, release or deployment is changed.
