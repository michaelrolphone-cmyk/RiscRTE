# Standard I2S TX, firmware 0.1.8

This is a generic CPU transport prerequisite, not an alarm implementation. It
reuses the unchanged `platform.i2s.controller@1` table from `TWatchPlatformV1.h`
and exact `audio.i2s@1` typed materializer. The selected provider receives a
device-scoped table only. Standard TX on physical I2S0/1, admitted BCLK/WS/data
pads, stereo signed16 and rates 8000/16000/22050/44100 are accepted; RX/PDM and
unselected controllers/pads fail closed. No chip identity, waveform, schedule,
speaker gain, PMU register or Watch pin is in firmware. Legacy GPIO/I2C/SPI and
sleep ABIs are unchanged. Unsupported selected RX/PDM fails binding before I/O.

## Ownership and bounds

The runtime owner task serializes every operation. The pad ledger reserves all
three pads; tokens never repeat. Open reserves before native setup and rolls back
on clean failure. A failed partial open whose cleanup cannot prove idle returns
false **with a retained token** and poisons the port; the external driver must
keep that token for close. No new work is permitted after poison. Close becomes terminal for writes, including after a failed close. Close alone
may clean the token/pads; poison remains a terminal app-retention barrier.

Writes accept at most 256 stereo frames/1024 bytes and 1..40 ms. IDF copies the
caller buffer synchronously into owned internal DMA; no app stack pointer is
retained. Native calls use `i2s_write(..., ticks_to_wait=0)` under one absolute
microsecond deadline, with at most 42 bounded attempts and one-tick scheduling
rounding. Partial counts are reported, never retried from the beginning. The
native adapter enforces frame alignment and bounds of returned byte counts.
The raw API returns success only for the full requested frame count.

The pinned IDF implementation takes an unbounded TX mutex internally. This is
uncontended by contract: only this serialized runtime owner uses these I2S units,
no ISR, Arduino audio stack, other task or concurrent call. This port is not safe
for sharing with an independent SDK owner. Close requires the install core so
interrupt teardown cannot enter cross-core blocking IPC.

Two driver-owned DMA buffers contain 256 frames each, initialized by IDF. Set
`tx_desc_auto_clear=true` so underrun cannot indefinitely repeat stale samples.
This is not an immediate mute. A successful write proves queued copies only,
not audible output or drained transmission. Successful close cancels queued audio.

## Stop independent of display

Close stops only the I2S DMA channel, attempts all three pads LOW independently,
and uninstalls the I2S driver only after stop succeeds. Safe LOW means staging
`gpio_set_level(0)` then ordinary output mode with both pulls disabled; SDK
`gpio_reset_pin` would enable a pull-up and is deliberately not used. Failed
stop, uninstall or any pad operation retains state/token for retry. No buffers
are freed after failed stop. No `i2s_zero_dma_buffer` call is used: that helper
can make an unbounded write. No SPI drain or display callback is invoked.

While an I2S token exists, Light/Deep entry and app finalization are refused.
This prevents unloading a caller or sleeping while DMA remains owned. Ordinary
I2C register operations and I2S close do not depend on an unrelated held SPI
transaction, so external haptic/speaker stop remains available after display
failure. Generic sleep/native retention semantics otherwise remain unchanged.

## Verification and limitations

`bash test/run_i2s_test.sh` runs the production CPU port plus the production native
adapter through SDK declarations/fault injection. It includes actual JSON/manifest admission with no hardware I/O and covers
selected scope/pads/owner and
stale tokens, frame/rate/budget bounds, partial/error/invalid counts, failed open,
stop/uninstall/pad cleanup, same-core enforcement, no replay, sleep barriers,
retained cleanup and unrelated pending display. `SANITIZE=1` adds ASan/UBSan.
An SDK shim is not execution of hardware DMA or proof of physical loudness,
electrical levels, power consumption or wake reliability.

Pinned implementation sources: [I2S, IDF4.4.7](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/i2s.c),
[GPIO](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/gpio.c),
[GDMA](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/gdma.c),
[interrupt teardown](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/intr_alloc.c).
IDF returned install errors normally self-unwind. Some internal early DMA-object
allocation failures may assert while deleting uninitialized queue/semaphore
objects. Mocked install-error coverage does not prove recovery from every real
SDK allocation failure. A target build and explicit physical qualification are
still required; no hardware, merge, release or flash is implied by these tests.
