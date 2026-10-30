# Native DMA resource fixture (test-owned)

This opt-in extra-source/app_main-wrapper build reuses the actual NativeHardware
DMA and resident-worker authorities. Ordinary firmware startup is unchanged.
The workflow builds only: it never flashes or runs a device. A successful build
means eight compiled cases, zero executed cases until actual authorized output
with the exact source marker is collected.

Cases: invalid route; actual allocation identity/clock; foreign scope; actual
foreign task owner refusal using the resident worker; disabled IRQ sublease
release refusal and actual retirement; stale generation; SDK SPI trigger conflict
and independent AES route with retained group clock; shared channel exhaustion.
SPI/AES are allocator route claims, NOT running SPI/crypto hardware consumers.
There is no transfer engine, forced IRQ event, injected allocator/free failure,
actual board/provider route-policy proof, or physical coexistence claim.

All test handles/tokens and callback storage are global/resident. Failed or
unknown allocation, disconnect, deletion, join or retirement parks without
releasing uncertain state or returning to normal startup. External SDK channels
are checked idle with no enabled/raw interrupt before deletion; connected routes
are disconnected first. The IRQ is allocated disabled and never enabled; observing
successful retirement does not prove a fired ISR/callback lifetime. The fixture
reads the pinned S3 group clock bit but never resets/toggles the shared clock.
It does not assert that the clock must switch off after release (other owners may
legitimately retain it). Early resource conflicts/heap errors are unqualified,
not silently converted into successful exhaustion or skipped cases.

On a supported authorized device, retain all `DMA_PROBE source=...`, per-case and
terminal records. `result=unqualified retained=1` is not cleanup success. Do not
invoke a guessed device, heartbeat route, or simulator as a substitute for this
actual binary and exact source identity.
