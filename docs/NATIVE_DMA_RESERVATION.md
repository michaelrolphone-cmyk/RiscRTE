# Native shared DMA reservation candidate

`platform.dma-reservation@1` is an explicitly selected, device-scoped native ABI2
capability. It uses the pinned ESP32-S3 IDF6.1 shared AHB GDMA allocator, not a
second physical allocator. No transfer engine, global controller reset, RX
allocation, firmware UI or automatic board selection is introduced.

## Route and owner authority

A compiled board policy `selectedDmaRoutes(validated_device)` must explicitly
return a nonzero semantic route mask for the already admitted hardware device.
Absent/unknown/zero policy fails capability binding. Default native production
therefore grants no routes. Instance-scoped tokens are NOT route authorization.
The fixture policy grants LCD only to its exact typed test devices; it does not
configure any deployed display. Engine integration must supply its reviewed
board/provider policy; this is separate from allocator lifecycle qualification.

Core reserve/release/IRQ operations require the native owner task. Native calls
also reject ISR, critical/non-yieldable and scheduler-not-running contexts.
IRQ custody additionally requires the same pinned creator task and core. Tokens
are nonzero monotonic generations, scoped to the selected device. Failed reserve
may return a token owning partial resources. Never discard it before successful
release. Success with an impossible null native allocation remains quarantined.

## Lifetime and failure

The SDK retains shared channel/trigger arbitration and group clock references,
including coexistence with native SPI/crypto clients. Consumer code owns actual
transfer registers/descriptors and must stop TX, disable/acknowledge its channel
interrupts before release. Release independently checks descriptor FSM idle,
channel interrupt enable and raw pending status; these bits are NOT proof that
an arbitrary external peripheral ISR has returned.

The route-matched optional IRQ sublease is disabled initially, nonshared and
non-IRAM. Its live or uncertain state blocks reservation release. Cleanup calls
remain available after CpuPort poison/fencing; new reservations, IRQ claims and
enabling do not. Failed disable/free/disconnect/delete retains custody and code.
The pinned SDK's same-core nonshared free path removes its handler under the
interrupt allocator lock before returning; calling from its pinned task avoids
cross-core IPC ambiguity. Native execution of this guarantee remains unobserved.
Independent external IRQs remain the consumer's teardown responsibility.

## Qualification boundaries

Host tests compile the actual core, native adapter against declared SDK mocks,
CpuPort/Runtime binding and real dynamically loaded ABI2 provider/application.
They prove clean teardown versus failed IRQ-retirement code/dependency retention,
not physical IRQ execution or DMA transfer visibility. The -M scanner pins even
pragma-system repository headers and refuses undeclared external includes and
compiler include-environment overrides. Standard headers retain toolchain hashes.

The focused workflow compiles actual CpuPort and NativeHardware with the pinned
SDK; the existing target verifier retains all worker checks and additionally
requires DMA headers plus internal-DRAM authority/slot symbols. Build-only proof
is distinct from target execution. No device is flashed or run by this workflow.
The existing worker/storage/sleep lifecycle is preserved, including held-resource
refusal. Current worker and cache physical acceptance gaps are not new DMA
prerequisites, and no deployed display/engine support is claimed here.
