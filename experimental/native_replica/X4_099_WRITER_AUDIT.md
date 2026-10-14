# X4 .99 settings writer boundaries

Source basis: product `199e231d962c401c5d20617e2f5e4da07bffd780`, Runtime
`1c1ed8661ea30d47e7e9707c41d492ce3a2b4d70`, System
`49752a02746b38afbcc403bb92189be5a8356c9d`, Utilities
`5c91c3517aa9f992e24fc453855701ec5a992355`. This is the frozen production
cohort, distinct from experimental System `a5bd60f`.

The exact authority inventory is reproducible with
`test/audit_native_settings_099.py`. It verifies all 106 store files against
the .99 composition receipt: 27 apps, 33 providers, 20 app grants for namespace
1 and five for namespace 6. Grants are an authority upper bound, not evidence
that every holder writes. No product selection is changed by this audit.

## Namespace 6: Wi-Fi profiles

Wi-Fi Settings is the identified semantic writer. Explicit Save, confirmed
Forget and successful user-started Connect with Remember selected write the
profile. Scan, selection, loading and count reads do not persist changes.

Relevant System code: `Apps/wifi_settings_portable.inc` (`wifi_activate`,
`wifi_profiles_step_owned`, `wifi_forget`), `PortableWifiProfiles.h` and
`PortableWifiCredentials.h` under `lib/PortableApps/include`.

The logical record consists of `wifi.profiles`, slot-zero `wifi.a0`, `wifi.a1`,
`wifi.b0`, `wifi.b1`, `wifi.commit`, and indexed `w%08x.a0/a1/b0/b1/c` records.
Count reservation, inactive chunks and selector must publish atomically; Forget
must include its tombstone and cleanup. Slot zero remains the network-tool
default. There is no separate selected/default-profile pointer to migrate.

Files, OTA, App Store and Daily Digest are read-only profile consumers. Files
uses `PortableNetworkSession`; the other tools use `PortableWifiSavedNetwork`.
All must read the same Core as the new Wi-Fi writer. Runtime's explicit
app/provider KV1/KV2 read-through supplies that route without broadening grants.

Experimental `a5bd60f` stages complete profile mutations plus one semantic
intent, captures Remember before Connect, and refuses legacy fallback. It
cannot forget an unreadable slot without trustworthy logical identity. That
differs from .99 numeric-slot deletion and must remain explicit in qualification.

## Namespace 1: explicit controller actions

- Settings: `settings.inc::settings_activate`,
  `settings_timezone.inc::stz_activate`, `settings_native_time.inc::snt_save`
  under System `lib/PortableApps/src`. Selected keys are `time_format`,
  `time_zone`, `desk_clock_face`, `desk_direction`, `reader_flip_ui`,
  `reader_language`, `sleep_idle`, and `rtc_basis` after confirmed Set Time.
  The selected Settings ELF has no `sleep_mode` or `alert_mode` key; its idle
  timeout is the only selectable timer. Preserve the distinct physical RTC,
  native-clock and `rtc_basis` outcomes; a Core transaction cannot make the
  physical clock write atomic.
- Home/shared Quick: `quick_adapter.inc::quick_apply`,
  `quick_session.c::session_apply/paper_light_apply/paper_tone_apply`, and
  `quick_radios.c::pqa_radios_apply`. Keys are `brightness`, `quick_bright`,
  `frontlight_tone`, `alert_dnd`, `contexts_on`, `quick_radio`. `quick_bright`
  is restore metadata belonging to the same OFF action, not another intention.
  X4 visual-only outputs disable generic audio-volume controls. Preview,
  temporary torch and hardware restoration do not persist preferences.
- Battery: Utilities `Apps/battery_power.inc` explicit broadcast toggle through
  System `PortableBroadcastClient.h::portable_broadcast_set_enabled` writes
  `ble_broadcast`.
- BLE Scanner: Utilities `Apps/ble_scanner.c::save_name` through
  `ble_sensor_names.h::ble_alias_save` writes the 40-byte alias/tombstone key
  `bs<address-type><12 hex address digits>`. `Apps/ble_scanner_paper.inc::paper_enable`
  writes `quick_radio` after explicit Enable Bluetooth. Failure compensation
  can restore the previous value; treat it as rollback of that operation,
  with a revision check, rather than another user intention.
- Contexts: Utilities `Apps/contexts_controller.inc::cu_action` and
  `Apps/contexts.c::ctx_fingerprint_setting` write `contexts_on`,
  `context_sources`, `context_timing`. Models/rules are app-data, not these
  preferences. The available `ctx_p0`–`ctx_p7` preset-save helpers have no
  demonstrated selected UI caller and are not counted as confirmed writers.

## Automatic changes are separate

Home `PortableLowBattery.h::portable_low_battery_observe`, called by
`quick_adapter.inc::low_battery_poll`, writes `battery_edge` before applying
`sleep_idle`, `sleep_deep`, `brightness` unless already OFF, and `quick_radio`.
Rearming writes `battery_edge` only. Preserve the existing consumed-crossing
behavior, including no replay after partial failure.

Home `contexts_policy.inc::contexts_tick` applies rules/presets through
`contexts_rules_adapter.inc::contexts_rules_tick` or
`PortableContextsClient.h::portable_contexts_apply_room`. Foreground Contexts
`Apps/contexts.c::cs_rules_tick` uses
`PortableContextActions.h::portable_context_setting`. These automatic paths
share setters with UI actions; a generic legacy put must not manufacture user
intent. The Contexts editor mask `0x274` permits idle timeout, Wi-Fi, Bluetooth,
brightness and DND. Home's rule loop does not itself apply that editor mask;
preserve or explicitly resolve that distinction before activation.

No boot/default-repair preference writer was found. Missing preferences use
virtual defaults. Invalid radio policy can turn hardware off without rewriting
the stored record.

## Readers and exclusions

All 20 namespace-1 app grant holders require same-Core reads. Bound
`alarm-service` reads `alert_mode`, `alert_dnd`, `time_zone`; `civil-clock-x4`
reads `time_zone`. Their bindings remain read-only. Runtime routing retains
existing grant, key, namespace, blob-size and invocation checks.

GameBoy is a reader. Product recovery replay pins source `a579dedd2a76956c7dcd654434cff9a556beb37c`;
build ledger and compaction custody bind it to selected .99 ELF prefix
`4e3f218f`. Its `capability/backend.cpp::begin_application` reads timezone,
time format and brightness; temporary brightness changes affect hardware only.
[Source](https://github.com/michaelrolphone-cmyk/T5S3-GameBoy/blob/a579dedd2a76956c7dcd654434cff9a556beb37c/capability/backend.cpp).

HID assignments use namespace 11; stopwatch/alarm records use namespace 2;
countdown records use namespace 3. They are not namespace-1 migrations. Watch
has no SD and is outside this X4 policy.

Before namespace 1 is selected, add atomic submissions at the demonstrated
controller boundaries, batch subordinate metadata, and preserve automatic and
rollback origins separately. Fence legacy writes instead of silently bypassing
the Core. Conflicting native/SD datasets remain preserved until source selection
is resolved; this audit does not choose a merge policy.
