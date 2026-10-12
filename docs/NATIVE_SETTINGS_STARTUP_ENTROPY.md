# Default-off native settings startup identity source

`RISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1` requires the selected X4 settings backend.
Watch and ordinary/default builds do not compile this source into the product.
This checkpoint is source/host/target-compile qualification, not physical entropy
or a firmware activation claim.

## Early hardware window

The sole capture call is the first action after Runtime records its setup task.
It precedes failure-evidence setup, Runtime/CPU ports, graph admission and every
ordinary provider. The pinned Arduino 2.0.17 / IDF 4.4.7 ESP32-S3 header explicitly
permits `bootloader_random_enable()` in early app code before RF initialization.
It continuously mixes SAR ADC noise into the HWRNG. Capture calls
`esp_fill_random` for 32 bytes, then always calls `bootloader_random_disable`
before checking the result or returning to RF/ADC initialization. It never
re-enables the source later. No radio is started for settings.

The selected X4 pre-setup composition was inspected: the native app-main wrapper
handles rail GPIO, RTC boot records, NVS and trace memory; `initVariant` handles
records and trace allocation. Arduino initializes clocks, PSRAM, logging, NVS
and unused-Bluetooth memory release; its weak `init` is empty. It does not start
RF or an ADC driver. The input hashes and source paths are preserved in the
qualification receipt. This ordering must be rechecked for any new product
wrapper, variant, static constructor, SDK or early peripheral change; a board
macro alone is not proof. Both enable/disable symbols are in the pinned target
SDK. No target is enabled by this implementation.

## Native identity derivation and failure behavior

A permanent INTERNAL 32-byte seed feeds SHA-256 over a fixed 16-byte versioned
domain, the seed, the exact namespace (1 or 6), purpose, and a global nonwrapping
32-bit boot counter. The first 16 digest bytes form an identity. Namespace and
purpose separate initial native incarnations from first-checkpoint identities;
every accepted derivation consumes a serial even if hashing or final custody
fails. At UINT32_MAX further derivation refuses. Collision probability remains
that of a 128-bit identity; host distinctness tests are not an entropy proof.

The seed is never persisted, logged, returned, supplied by an application, or
derived from a MAC/card/clock. A lock-free 32-bit gate rejects concurrent and
recursive operations before callbacks. ISR, wrong namespace/purpose, missing
custody, zero capture, all-zero identity and hash errors refuse without copying
an output. Scratch and hash contexts are wiped on every return. No dynamic
allocation is used by this module; the pinned SHA implementation is unchanged.

The concrete backend obtains two initial incarnations during native-owner
preparation. If entropy is absent or fails, both candidates are wiped and the
backend still exposes existing authoritative storage with a null initial
incarnation. Its guarded first-checkpoint callbacks remain bound: a durable
first-creation marker can resume using its recorded identities, while any fresh
identity request refuses. Existing native/SD data is never hidden or replaced by a synthetic
identity. Operations requiring a new incarnation or first SD lineage defer.

For a first checkpoint, the callback requires actual namespace worker custody
and the fixed fenced mount. Its independent first-checkpoint guard requires the
exact native coordinator lease on owner and worker throughout the attempt. The
selected Runtime routes all namespace writes through that coordinator; private
files share the permanently fenced mount. Real SD capacity is proven by the
new typed capacity suffix and its worker receipt, not a callback estimate.

## Qualification limits

Host tests model the SDK random input and owner task, using real OpenSSL SHA-256.
They cover 1,001 distinct sampled IDs, paired enable/disable, source failure,
ISR, namespace/purpose, wrong custody, reentry, hash error and counter saturation.
The concrete backend runs against the pinned real ESP LittleFS profile, normal
and ASan/UBSan, including both namespaces, missing entropy, import, no-mount,
wrong owner/worker/namespace, NVS refusal and retained physical flash failure.
Factory/worker boundary doubles remain explicit in that focused backend suite.

Pinned Xtensa compilation includes the selected entrypoint/backend/source; the
source is empty when disabled. Default entrypoint code/data remain identical
with debug line metadata stripped. Local stack reports are lower bounds, not a
whole worker-stack proof. Physical RNG qualification, full resource/latency
measurement and end-to-end product/update qualification remain activation gates.
