# Optional scenes and product capacity

## Runtime 0.2.0 prototype increment

This increment supplies `sdk/app/RiscSceneV1.h` and `RiscSceneStateV1.h`.
The native runtime does **not** render a view, start a UI, acquire a display,
or require `ui.scene`. A deployment that does not select the optional provider
has the same headless execution model and default capacity as before.

`ui.scene@1` describes bounded semantic routes, text, fields, links, named
actions, value changes and suspend requests. It contains no drawing commands,
pixel formats, input-device IDs or form-factor categories. Providers copy the
whole declaration and return copied events; no controller callback or borrowed
string survives a call. Session handles belong to one live controller and are
never checkpointed.

The navigation checkpoint is a logical route/focus path, not rendered pages.
`RiscSceneStateV1.h` encodes a versioned, little-endian, checksummed envelope.
Applications own their payload schema and persistence policy. Rehydration needs
a newly acquired session; old grants, handles, pointers and storage generations
must not be restored. The current Alarms prototype persists on graceful suspend
and before domain mutations. It does not promise to save every keystroke before
an arbitrary power loss.

A separately packaged presenter is implemented in RiscRTE-System-Apps. Alarms
and its presentation-independent control service are in RiscRTE-Utilities.
Current scene implementation is single-client and bounded: 8 routes, 24 nodes,
8 logical navigation entries, copied labels/text and serialized service calls.
This is a prototype ABI, not a claim of a complete general-purpose UI toolkit.

Terminal and remote web implementations may later provide the same capability.
No terminal transport, HTTP server, network discovery or remote authentication
is introduced or required here. A terminal/web device selects a presenter only
when needed; a genuinely headless deployment selects none.

## Explicit graph capacity

Existing full Watch stores already select 24 providers. The prototype adds a
presenter, a presentation profile and an alarm-control provider. Select the
following in the **current product native build**, with PSRAM metadata placement:

```
-DRISC_RUNTIME_PROVIDER_CAPACITY=28
-Wl,-u,risc_runtime_provider_capacity
```

The override supports 1..64 providers. Larger ESP graphs require the already
supported paired-bank or PSRAM-metadata configuration. Foreground grant headroom
is preserved: the 28-provider configuration has 44 graph grants. Without the
override, existing 17/32 and 24/40 provider/grant bounds are unchanged. This is a
generic graph option, not a UI dependency or a product-specific native driver.

The linked `risc_runtime_provider_capacity` witness records the actual count.
Verify it from the resulting native ELF, not a proposed build-command string:

```
python scripts/runtime_capacity.py path/to/firmware.elf
python test/runtime_capacity.py
```

The 13 compile tests include unchanged headless defaults, paired/PSRAM targets,
valid explicit capacities and rejected out-of-range/internal-DRAM selections.
The cross-repository integration harness links the production Runtime with the
28-provider option, real unloadable controller/service ELFs and physical-device
doubles. It checks normal and demand-retained lifecycles as well as a boot graph
that contains no UI/display/input provider.

## Product integration

Do not relabel an existing 24-provider native binary as an expanded build.
Likewise, changing `boot.json` invalidates the old product/cohort binding.
Product staging removes that stale binding and records the unchanged source
store. The product's native/cohort/image builder must bind the new components to
its current native implementation before producing a flashable image.

This Runtime branch is based on the publicly accessible 0.1.83 source. It is not
a substitute for later product-native changes. In particular, X4's current
0.1.96 source pin must be made available or receive this small generic capacity
change on its actual working branch before assembling that current full product.
