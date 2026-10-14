# First configured-default entry

The optional `initial_default` Runtime API suffix is read-only and preserves every existing prefix. Clients check `RISC_RUNTIME_INITIAL_DEFAULT_V1_SIZE` and the callback before use. It returns true only on the owner task during the first configured-default `app_main` in this Runtime session. Init, fini, child apps, later default returns, retained custody and queued handoffs return false. Failed init does not consume the first entry.

This is an invocation fact, not a hardware wake classification. Shared applications combine it with the retained-wake capability when choosing a cold-boot presentation. Runtime contains no presentation or product logic.

Validation: default-request lifecycle matrix under normal and ASan/UBSan builds, including owner mismatch, default-as-caller/receiver, failed init/load, automatic revocation and retained exits. Frozen Runtime and file-open prefixes remain unchanged.
