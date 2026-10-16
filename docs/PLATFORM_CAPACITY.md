# Explicit native platform table capacity

`RISC_PLATFORM_CAPACITY` keeps the historical default of 32. A product may
explicitly select 40; every other value is a compile-time error. This changes
only the bounded native table storage, not registration checks, graph admission,
provider grants, application policy or SDK interfaces. Complete prepare still
fails closed when the selected table capacity is exhausted.

The preserved X4 provisioning graph needs 34 tables, including its explicit
provision-request/package-volume bindings and Runtime-owned realtime/installation
tables. Its product profile selects 40 and must qualify the final native memory
cost. No board or package identity infers this selection.

The allocated `risc_platform_capacity` marker records the compiled capacity.
Products requiring linked evidence retain it with `-Wl,-u,risc_platform_capacity`
and verify its bytes in the final ELF, together with all Runtime compile flags.

`python3 test/run_runtime_platform_capacity_test.py` covers default32 and
explicit40 registration limits, duplicate/context rejection, and real prepare
with the additional realtime/installation registrations. Repeat with
`SANITIZE=1`; optional `RECEIPT` preserves the commands. Eight invalid selections
fail compilation. These tests never activate a driver or access hardware.

`test/compile_runtime_platform_capacity_target.py compile_commands.json OUTPUT`
compiles two small probes with the selected native Runtime command, changing only
the table capacity. It records target `sizeof(Runtime)`, object/source hashes and
the real retained allocator reference. Eight slots add 960 bytes with Xtensa
GCC15. Paired native main retains the `NativeRuntime.h` allocation, which requests
`MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT` without internal-heap fallback. This is an
allocation policy and object-size proof; final linked placement, PSRAM/heap
headroom and IQ-bank margin must still be qualified by each product's native CI.
