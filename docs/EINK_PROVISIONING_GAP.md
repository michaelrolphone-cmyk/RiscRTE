# E-ink product packaging: concrete remaining dependency

The generic provisioning engine can install an admitted e-ink product store,
including its pinned `board.json`. The current X4 heartbeat fixture is **not** a
Reader product. No fully functional Reader profile is published by PR15.

## Verified existing source

Reader PR350 (`xteink-x4-pro-boot`) has independently packaged ordinary ABI2
providers and `scripts/stage_x4pro_packages.py`. Its `Apps/default.c` is a small
entry wrapper: it imports `t5_reader_entry_get_api` and invokes the host's
`pump()`. `src/native/NativeReaderEntry.cpp` implements that API inside Reader
firmware, calling the existing ActivityManager, renderer and Reader main-loop
callbacks. The built `default.elf` retains that unresolved host import. Its
manifest targets Reader firmware 1.3.90+, not minimal Runtime's app contract.

Minimal Runtime intentionally neither exports that Reader entry API nor links
Reader UI/activities. Its production ELF admission refuses the unresolved
import. Merely copying the Reader default ELF, or substituting the generic
heartbeat, would not deliver the requested functioning e-ink device.

Useful source:
- [Reader entry ELF](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/xteink-x4-pro-boot/Apps/default.c)
- [Current host entry implementation](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/xteink-x4-pro-boot/src/native/NativeReaderEntry.cpp)
- [Existing X4 package stager](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/xteink-x4-pro-boot/scripts/stage_x4pro_packages.py)
- [Panel provider](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/xteink-x4-pro-boot/Drivers/x4pro_panel/manifest.json)
- [SD provider](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/xteink-x4-pro-boot/Drivers/x4pro_sd/manifest.json)

## Smallest product-owned completion plan

1. Package the existing Reader activity/renderer/document engine as the product's
   `default.elf` (or a product-owned software provider called by it). Retain its
   current UI and behavior. Replace the firmware entry callback with
   `risc_runtime_get_api` lifecycle/cooperation/handoff; do not add a firmware UI
   or export a monolithic Reader engine from minimal Runtime. The relevant
   sources are `src/activities/reader`, `src/activities/browser`, ActivityManager,
   `lib/GfxRenderer`, `lib/Epub`, `lib/Txt`, and their actual dependencies.
2. Reuse the already extracted X4 panel/buttons/frontlight/I2C/touch/battery/RTC/SD
   provider sources, protocol state machines and tests. Their current X4
   manifests use a board label and several providers contain fixed-pin MMIO.
   Give each hardware provider an exact typed `hardware.device@1` record,
   compatible/revision/config declaration and explicit bindings before admitting
   a full board graph. Board labels alone cannot substitute for chip identity.
   Preserve the existing display.output, input.navigation, input.touch.raw,
   board.battery, rtc.clock and storage.volume interfaces where compatible.
3. Close the actual storage/controller gap without formatting or SD-first
   bootstrap. Existing X4 SD code is native one-bit SD plus provider-owned FatFs;
   it is not the `storage.sd-spi` configuration already materialized by Runtime.
   Reuse its protocol and filesystem code behind explicit owned GPIO/controller
   mechanics. If an additional typed materializer is required, version it
   explicitly and retain the shared hardware ABI. The flash seed still boots
   networking before any removable-storage driver is available.
4. Author the complete X4 product `boot.json`, real `board.json`, driver/app
   manifests and exact namespace policy, then run ordinary graph/import admission
   and real host lifecycle tests. Remove or adapt Reader-host-only imports using
   the existing declared capabilities; do not relax Runtime's import allowlist.
   Verify external state retention, app handoff, storage ownership, missing media,
   failed driver start/stop, and interrupted full-store provisioning.
5. Build the product store against the exact verified Runtime candidate, bind
   cohort metadata to that firmware, publish its files to an immutable product
   commit, and generate its credential-free inventory with
   `scripts/provision_profile.py`. The same tested first-install and provisioning
   path then applies without a second downloader or bespoke e-ink installer.

This is concrete product extraction/adaptation work still required. Software
admission, target builds and host models do not establish physical panel, SD,
power, wake or battery performance. Those hardware checks remain separately
unrun; they are not used to hide the missing software product artifact.
