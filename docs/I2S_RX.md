# Bounded PDM input, firmware 0.1.12

This adds one generic CPU transport to the existing unchanged
`platform.i2s.controller@1` table and `audio.i2s@1` configuration. No application,
FFT, recording, waveform, microphone identity, watch pin or speaker policy lives
in firmware. Standard TX remains available on I2S0/1. Selected PDM RX is accepted
only on I2S0, with `ws=-1`, mono signed16 PCM and 8000/16000 Hz. The external
provider must explicitly select the immutable controller and pads. Board graph
validation already rejects duplicate controllers, overlapping pads and RX on
I2S1 before any hardware I/O. Unselected devices receive no raw table.

## Native setup and ownership

The native boundary appends separate RX callbacks; existing TX callbacks and
all ELF-visible SDK layouts remain unchanged. Only the serialized runtime owner
may call the transport. No independent SDK, ISR, Arduino audio stack or other
task may share its I2S units. Transfer and teardown require the install core.
IDF's internal RX mutex uses an unbounded wait; the exclusive owner contract is
what makes it uncontended, not the transfer deadline.

RX uses MASTER|RX|PDM, 16-bit ONLY_LEFT mono, two 256-frame DMA buffers, no APLL,
and explicit 128x decimation (`I2S_PDM_DSR_16S`). Its PDM clock is nominally
1.024/2.048 MHz at 8/16 kHz. Under the pinned IDF4 interface the single clock in
the `bclk` configuration field is connected through `ws_io_num`; BCK, MCLK and
DATA_OUT are disabled. This is an explicit transport translation, not a layout
reinterpretation. Initialization sets decimation before attaching the clock pad.

RX DATA is configured as digital input, with both pulls disabled, before setup
and during every cleanup attempt. It is never driven LOW or put into output
mode. The clock is staged LOW with the existing RTC/deep-hold-safe pin helper.
Close stops only its I2S DMA, independently attempts DATA input and clock LOW,
and uninstalls DMA resources only after successful stop. Failed pad cleanup,
stop or uninstall keeps the native state and raw token reserved for retry.
The separate TX controller and display/SPI are not drained by RX cleanup.

## Read bounds and errors

Read accepts 1..256 frames (at most 512 bytes), a caller-owned output buffer, an
actual-count pointer and a 1..40 ms budget. IDF copies synchronously; it retains
no caller pointer. Each SDK queue wait is zero, with one absolute microsecond
deadline and at most 42 attempts. Scheduling may round by at most one tick.
The adapter advances the destination after every partial copy, checks byte
alignment/count bounds, and never restarts at the original destination.
Success means every requested frame was copied. On failure the count reports
only the valid copied prefix; the unfilled tail is not fresh PCM.

Both RX and TX raw calls become terminal for transfers after a native error or
short transfer. Close is then the only stream operation permitted. A malformed
backend frame count additionally poisons the port. Invalid caller arguments,
stale tokens, direction mismatches and wrong-task calls perform no native I/O.
A failed open attempts close; a cleanup failure returns false with a retained
cleanup token and poisons the port. Tokens and pads are released only by proven
successful cleanup, while poison still prevents later launches.

This is a bounded block-capture interface, not a lossless recorder. Delayed
consumers, display work or synchronous storage calls may overrun the finite
native buffers. The ABI has no overflow timestamp or continuity guarantee.
Applications must not interpret missing blocks or partially filled buffers as
continuous, complete sample history.

## Healthy streams and storage safety

A healthy live I2S stream no longer revokes boot-session provider-bound storage.
As with a healthy station-radio session, activity and failed cleanup are distinct:
`providerStorageSafe` rejects closing/failed I2S, poison, native transfers, retained
holds and sleep transitions. Every live I2S token still blocks app finalization,
launch handoff and Light/Deep sleep. A closing I2S stream reports RETAINED to
sleep callers, while a healthy one reports BUSY. All selected I2S closing
states are checked before live tokens, so a healthy stream cannot mask a failed
stream on the other controller, regardless of their selection order.

The first bound-storage call while unsafe still revokes all live provider storage
contexts permanently, including after the stream subsequently closes. No broker
or authority-lifetime code is weakened. Clean recovery before another storage
call avoids that revocation; successful physical cleanup alone never restores
already-revoked authority. App cleanup must finish before `app_main` returns:
the existing runtime retention barrier precedes `app_module_fini`.

Product-level sharing of a speaker with an alert service remains external policy.
A foreground app must stop its own stream before the service begins its output,
and must not close a stream later acquired by another consumer of that provider.
Nothing here grants output preemption, recording or persistent app authority.

## Verification and remaining qualification

`bash test/run_i2s_test.sh` includes existing TX tests, native RX fault injection,
CPU RX scope/direction tests, real JSON/manifest admission, and 22 real
Runtime/Graph/dlopen lifecycle scenarios covering TX and RX bound KV, healthy
live-stream retention, partial/error transfers, failed opens, retryable/failed
close, stale authority after cleanup, and app/child/fini/unload barriers.
`SANITIZE=1` enables ASan/UBSan. In a ptrace-based executor, run with
`ASAN_OPTIONS=detect_leaks=0`; that does not qualify leak detection.

Sources inspected against pinned IDF4.4.7:
[I2S driver](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/i2s.c),
[HAL defaults](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/i2s_hal.c),
[S3 low-level definitions](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/esp32s3/include/hal/i2s_ll.h),
and [I2S API](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/i2s.html).
SDK install-error unwinding and allocation-assert limits described in
[I2S TX](I2S_TX.md) still apply. A host shim and target ELF validation do not prove
hardware PDM polarity, signal quality, electrical levels, DMA continuity, audio
latency, low power or physical quiescence. Target firmware compilation and explicit
owner-directed physical qualification remain separate gates. No hardware, flash,
merge or release is implied.
