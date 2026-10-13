# Quad SPI ownership and frame audit

## Established and unestablished facts

The owner reports visible output with the Waveshare vendor QSPI adapter, but not with the old raw adapter or the later complete framed-runtime images. The raw diagnostic did not call `spiTransferFrame`; its result cannot isolate a defect in that later API. SPI completion is not a panel acknowledgement. A physical root cause is not established by the changes below.

Runtime commit `5234b0de153c7ed744a8b8e1164bcc1b4867127b` changes quad SDK ownership to persist between CS cycles. That behavior is preserved: the first begin creates/acquires the SDK device (the requested frequency is known then), subsequent end calls drain and raise CS only, reconfiguration releases the old handle before replacement, and close releases before SDK-device removal. Pending transfers prevent release. Ordinary and three-wire behavior remains separate.

The pinned IDF 4.4.7 implementation of `spi_device_release_bus` clears the acquiring-device record, releases the lock and, when enabled, releases the power-management frequency lock. It does not call GPIO matrix or pin-configuration functions. Holding ownership is supported and removes lock churn, but a statement that release inherently remaps pins is unsupported. A persistent lease also retains SDK DMA/PM resources until close; sleep/current behavior requires its own qualification.

Source: espressif/esp-idf v4.4.7, `components/driver/spi_master.c`, `spi_device_acquire_bus`, `spi_device_release_bus`, `spi_new_trans`.

## Corrected frame regression

The 0f21d1c change removed variable header lengths from every framed write while retaining default 8-bit command and 24-bit address lengths on the SDK device. That violates `RiscSpiFrameV1.h`: a continuation explicitly requests zero header lengths. IDF then emits another 8+24 bits even when command/address values are zero. Normal raw writes also inherit those defaults unless explicitly suppressed.

The repair keeps fixed default headers for 8/24/0 transactions and inline data for payloads up to four bytes. Other requested lengths use the extended transaction fields with VARIABLE_CMD/ADDR/DUMMY, including headerless continuation. Raw quad callbacks now use extended zero-header descriptors. No board pins, CO5300 opcodes or panel dimensions are encoded in the generic transport.

MULTILINE_CMD/ADDR select the same lane width as the data. With single-line data they do not change the electrical line width. For a 1-1-4 write they must not be added to the QIO header. VARIABLE flags select lengths; they do not select multi-line mode.

## Software evidence

`test/run_native_spi_test.sh` now includes the SDK fields and inline-data union used by production. Tests check effective header lengths rather than assuming that zero-initialized extension fields are honored without flags. Tests also cover 64 CS cycles with one lease on both hosts, reconfiguration, arbitrary frame lengths, timeout retention, cleanup retry, and the existing single-line/three-wire regressions. Run normally and with `SANITIZE=1`.

These checks do not establish physical illumination or electrical waveforms. The next device comparison must use framed callbacks for commands and pixels, preserve a vendor control phase, and capture actual IDF pre-transmission registers and GPIO routing. The old raw test should not be reused as evidence that the framed path was measured.
