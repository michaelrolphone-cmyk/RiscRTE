# Board mapping and trusted port boundary

RiscRTE consumes `riscrte.board-hardware` schema1. The common header, contract and
Garden schema are exact Garden `7e30afc407c86f34364cbfa2035d6f7893fdea8c` copies.
T-Watch fixtures and six additive layouts are pinned to
`e48a540cdc2c4c1e642fbf20807e2743e2a679a9` (baseline1.0.0 source; no release is
claimed). No board-name dispatch, automatic RF variant selection, silicon
revision guessing or reflection-based config casting is used.

Each bus declares `controller_namespace`. `esp32.peripheral` retains the physical
peripheral number; optional `physical_controller` must equal `controller`.
`riscrte.logical` retains the logical number and requires `physical_controller`.
The ESP32-S3 port accepts physical SPI2/SPI3 and I2C0/I2C1. The mapper does not
add2 to a logical number. `Board::physicalController(bus_instance_id)` gives the
explicit resolved physical owner; the typed C `bus.controller` is unchanged.
The native owner translates physical SPI2/SPI3 to the SDK host enum explicitly.
Conflicting physical controller owners are rejected even through logical aliases.

Original catalogs without the additive metadata require an explicit owner port
declaration in boot.json. For example (only for the selected legacy catalog):

```json
"port": {
  "cpu_compatible": "espressif,esp32-s3",
  "controller_mappings": [
    {"bus_instance_id": 103, "controller_namespace": "riscrte.logical", "physical_controller": 2}
  ]
}
```

Any inline declaration must agree with that override; unused/duplicate mappings
fail. An override is not inferred from board ID, driver ID or numeric coincidence.
Updated canonical profiles contain their per-bus metadata and need no override.
The immutable baseline release descriptor is provenance metadata, not a substitute
for those per-bus declarations. Boot names one concrete profile path; variant
catalogs/default=null are not boot profiles and fail admission.

The fixed materializers support the seven original types and exactly these
additive v1 layouts from `TWatchHardwareV1.h`: controller.gpio, controller.i2c,
peripheral.i2c, power.axp2101, audio.i2s, radio.lora. Unknown type/version pairs
fail. Absent display/touch reset=-1 requires both delays0; present reset requires
both delays1..500ms. Drivers may reject a syntactically valid configuration more
strictly. Inactive bus pin keys may be omitted or explicitly integer-1; active
signals, addresses, controllers and exclusive GPIO roles are checked for overlap.
I2C address uniqueness is scoped to bus.instance_id. WiFi and BLE unit0 are
separate compatible-family resources; the trusted CPU port owns RF coexistence.
Console/USB/flash reservations belong to the selected port, not universal mapper
rules: baseline UART0 reserves43/44; a Watch mic at44 requires another explicitly
selected console port. The generic CPU port is described in [CPU_PORT.md](CPU_PORT.md); no Watch-specific
firmware or driver implementation is linked into it.

`Runtime::registerPlatform` is a compiled-in port API, not an ELF export. It accepts
bounded firmware-owned interface tables with explicit Device or Bus scope;
only platform.clock/platform.board may be Global. Native transport interfaces
(`platform.*`, `spi.bus`) cannot fall back to an arbitrary ordinary ELF. Contexts
must enforce the selected config and physical resource policy and remain pinned
through successful shutdown or retained failed quiescence. The baseline now registers a generic clock and only the selected GPIO/I2C/SPI
services through a compiled-in preflight hook. Unsupported transport operations
still fail closed. The independent-instance I2C regression and pinned external
clock integration exercise real modules through scoped transport contexts.

Ordinary dependency edges come from requirements plus explicit device bindings.
I2C dependency bus IDs must match; unused bindings, cycles, missing/ambiguous
providers fail before any ELF entry. Existing board.battery PMU ordering edges
remain present. A generic rail-lease capability is future contract work; this
migration does not silently remove power ordering or claim that a gauge API
expresses rail ownership.

One driver package may serve several selected instances. Package version/path
must agree; graph keys include package ID plus selected instance ID. Each hardware
load performs independent relocation/data/BSS allocation, even for the same path
or basename `driver.elf`. Package-only selection is ambiguous when multiple
instances exist. The provider ABI/driver ID is unchanged. Quiescence keeps the
exact image, typed hardware and dependency table alive until teardown succeeds.
Host OS tests map identical temporary copies because native dlopen caches files;
the target loader performs fresh relocation directly from the immutable store.
