# Standard I2S transport

`platform.i2s.standard@1` is a raw, board-bound standard Philips I2S service. It supports TX, RX and duplex at 8, 16, 22.05, 32, 44.1 and 48 kHz with two signed 16-bit slots and MCLK at 256 times the sample rate. The backend selects the explicit IDF 4.4.7 `mclk_multiple` field; it does not rely on an APLL-only fixed clock setting.

The typed `audio.i2s-standard` hardware description binds the controller, MCLK/BCLK/WS/data pins and optional amplifier control. The coupled codec control-bus addresses remain hardware metadata. Codecs, register sequences, gain and amplifier policy belong in loadable physical providers. No Waveshare or codec commands are compiled into the runtime.

A shared transport session owns both clock directions when duplex is selected. Transfers copy at most 256 stereo frames with a single 1..40 ms deadline. RX may return a successful short or zero count. Partial/failed TX cannot be replayed; only close is admitted after a transport error. Failed initialization returns a nonzero token when cleanup is incomplete. The same native I2S lifecycle/sleep barriers cover old TX, PDM RX and standard duplex. RX pins are never driven as outputs during cleanup.

Host tests exercise production Board/Runtime/CpuPort/NativeI2s paths, both controllers, all rates/directions, exact SDK clock configuration, pin collisions, failed initialization, partial transfers and cleanup retry. Existing TX/PDM lifecycle suites remain in `test/run_i2s_test.sh`. Target compilation is separate evidence; no measured audio quality, MCLK accuracy or physical codec bring-up is claimed.

The explicit universal app-data profile selects 40 provider slots, 64 installation rows and 24 immutable application policy/declaration rows. It does not add physical capabilities or remove existing apps to fit the new adapters. The existing targets retain their previous capacity selections. Device images must be composed against their selected native build before paired metadata is generated.
