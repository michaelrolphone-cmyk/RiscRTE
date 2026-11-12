# Resident attention / serial-transfer interaction

Runs the actual merged Runtime and host/child app DSOs with
`RISC_NATIVE_SERIAL_FILES=1`. Native capture safety, serial Port hooks and KV
are explicit doubles. Quiet observation, pending intent and manual rejection
must not invoke serial hooks, lifecycle dispatch or control callbacks, or cross
the capture exit barrier. Doubled native allocation-context hooks assert the
child memory selection remains unchanged throughout observation and rejection. Successful paths then stop capture, observe ordinary
exit, release grants, finalize both apps, and enter actual Runtime parked
serial checkpoint. Forbidden observer/reject work retains without later hooks.

This is not physical USB, production Home, native I2S/RF, production allocator or the NativeSerialFiles protocol. Retained transfer/storage
suites qualify those separate software boundaries. Child grant identity and
absence of lifecycle dispatch are checked here; physical input focus is not.
