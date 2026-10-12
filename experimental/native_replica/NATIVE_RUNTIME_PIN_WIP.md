# Work-in-progress Runtime graph pin and Port adapter

This is a source preservation checkpoint, not a qualified or enabled product
path. Only a host C++ syntax compilation has been completed. Behavioral,
retention, target and actual X4 composition tests have not yet been run.

NativeRuntimeStoragePin resolves the exact installation volume from the
prepared Runtime graph and holds its own provider grant. NativeSdPortAttempt
combines that pin with the opt-in real CpuPort custody seam and the experimental
NativeSdAttempt. Neither component is registered, constructed by production
startup, exposed to apps, or selected by a product build.

Open work includes: behavioral tests of graph pin lifecycle and error paths;
actual X4/FatFs/Port composition; checked warm session admission; release and
retention precedence; reentry and destruction contracts; target memory/ABI
checks; and the concrete production startup/maintenance coordinator. The
current adapter is fresh-only, so it must not be installed as a permanent lease
that blocks ordinary Files/USB use.

Default Runtime and Watch behavior remain unselected. No firmware delivery or
hardware qualification is represented by this checkpoint.
