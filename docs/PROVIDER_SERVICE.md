# Bounded synchronous provider service

`risc_driver_service_v2` appends an explicit tag (`0x53525631`, SRV1), version 1,
and `service(uint32_t budget_ms)` after the complete existing poll suffix.
The prefix, stream-session extension, provider ABI and app authority stay
unchanged. The loader checks tag, full size, version, callback and quiesce before
accepting this extension; unrelated larger tables cannot dispatch it.

This callback is for explicitly bounded synchronous work that cannot meet the
8 ms cooperative poll deadline. Runtime invokes at most one active provider per
safe point with a 1000 ms total deadline. It never runs from a diagnostic line,
observer or native storage drain. The graph's existing lifecycle/stream/poll
fence blocks recursive service, polling, acquisition, release and shutdown.
Revoked/retained providers cannot run. A callback with no pending work does no
storage/hardware I/O. The provider must check the deadline before each bounded
operation; an already in-flight bounded operation may complete after it.

Runtime services before/after provider acquisitions outside graph lifecycle,
before app entry and at ordinary owner yields. Native/app-data retained custody,
foreign owners, stream activity and promotion exclude admission. Cooperative
poll remains unchanged. The product owns file paths, log contents and cadence.

Host checks: `test/run_provider_service_test.sh` exercises real mapped providers,
tag/version rejection, no dispatch through poll, one-call fairness, invalid
budgets, recursive lifecycle rejection, and retained/revoked custody. Existing
provider graph, cold-start and diagnostic-source tests remain required.
