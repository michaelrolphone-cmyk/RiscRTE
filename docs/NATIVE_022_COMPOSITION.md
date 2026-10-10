# Runtime 0.2.2 source composition

This increment composes the public 0.1.106 diagnostic checkpoint source
274bc66f193cbe29018d2a85c9400cb0dce8aacc with the restored configured app-data
exports and opt-in TCP listener at 96e8c1f4e76ba3accfdff33d81b06233ecc5e7f5,
and the checked asynchronous Wi-Fi lifecycle at
fc05d35827fc0d51be749c6797be4b3a4426ba23. Its production Wi-Fi fix is
114f8035ae19539cc05f4511c1d74b7d513da398. The composition changes only the
version after that frozen source; no native capability is removed.

The Wi-Fi worker publishes copied request/status/cancel state, preserves the
advertised unavailable suffix after startup allocation failure, and uses an
owner/worker resource lease for direct services and flash access. The canonical
key-value ABI includes the additive BUSY result required by current app clients.
The retained-provider, file, USB, diagnostic and radio fences remain selected.
See docs/evidence/radio-async-20261010/receipt.json for all 58 actual application,
provider and native seam cases in normal, ASan/UBSan and ThreadSanitizer modes,
and the three target builds. Hardware stack/RF behavior is not yet qualified.

The native TCP listener remains default-off and the ordinary UI/Wi-Fi product
can build without it. This source does not expose a WebDAV product endpoint,
add the separate in-progress entropy capability, or include a BLE setup UI.
The later external temporal .107/.2.1 features are not silently imported or
claimed here; this UI/Wi-Fi increment retains its selected pre-temporal product
contract. Their separate composition requires the external owner's final pins.
