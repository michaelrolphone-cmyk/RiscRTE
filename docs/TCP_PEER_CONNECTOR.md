# Optional peer connector — focused integration patch

Base: `81845e8027b94abc5c7a36a45e0004884ada73b6`, itself based on the
current dynamic-installed Runtime `91324f41b5a79df21f6bc39a04899eba4d0be351`.
This source patch does not assign a new Runtime/product release or rebuild firmware.
It changes no capacity setting, installer worker, selector, hardware ABI or storage policy.

The provider-only TCP table retains its existing prefix and appends the optional
`RISC_TCP_CONNECT_TAG` suffix. Consumers must check the complete suffix size, tag
and both function pointers. Legacy backends expose a zero/absent connector.
`connect` takes an explicit IPv4 endpoint and returns an owned handle even when
pending. `connected` performs one zero-time readiness check and SO_ERROR query.
The caller supplies its deadline, cooperative polling, cancellation and close.
There is no DNS, retry, discovery, authentication, file protocol or network startup.
The four existing client slots are shared by accepted and outbound sockets.

Every connection uses the same activation generation, owner task, radio custody,
sleep exclusion, checked close and terminal retention rules as accepted sockets.
Cancellation while pending and close after network loss are supported. A failed
connect/readiness still requires checked close; an uncertain close is never retried.

## Qualification

* Production syscall shim: pending/ready/refused connections, capacity, cancellation,
  network loss, stale and cross-owner handles, owner loss at every setup/readiness
  syscall, failed close, and suppression of all I/O after retention passed.
* Runtime broker: legacy and extended backend, candidate admission, activation
  generations, raw app denial, sleep/restart exclusion and revoked callbacks passed.
* The production syscall test passed under UBSan on Darwin. The full broker UBSan
  run stops at its candidate-admission assertion; the exact unchanged base repeats
  that failure. This is a remaining test-harness/admission investigation, not a pass.
  No AddressSanitizer result is claimed.
* Current Runtime.cpp, CpuPort.cpp and production NativeTcpListener against the
  ESP32-S3/lwIP SDK compiled individually with the qualified 32-provider profile.
  No new complete firmware was built and no network or device was accessed.

The dependent ordinary System TCP facade supplies an equivalent checked suffix
while accepting the legacy native table size. Peer WebDAV, BLE central discovery,
and the broader Watch user volume remain separate unfinished integration inputs.
