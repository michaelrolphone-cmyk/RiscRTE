# Explicit software provider dependencies

Boot driver selections may include `bindings`, an array of exact `capability`, `api`, and `instance_id` records. These select only declared software dependencies; zero selects a singleton explicitly. Missing targets never fall back to another instance. Without bindings, the prior unique-provider rule remains.

Hardware drivers continue to use board bindings. Boot bindings cannot replace hardware mappings, trusted platform providers, raw SPI, or provider-bound private storage. Duplicate, undeclared, malformed, wrong-version and unavailable selections fail before activation. Graph cycle validation remains in force. No driver ABI or hardware descriptor changes.

Waveshare T023 adds an internal package volume beside SD. Its USB MSC software driver previously relied on the sole SD provider. The product now explicitly binds its storage.volume@1 dependency to SD12, preserving USB authority and exposing no internal package storage through USB.

Validation: existing Runtime lifecycle suite and new dual-provider admission tests pass normally on macOS and with Linux ASan/UBSan. Product native build and full store admission are separate gates. No hardware operation performed.
