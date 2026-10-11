# Native crown recovery checkpoint

This opt-in ESP32-S3 port change is based on Runtime
`c3d750d113dbc79af87daaad316ab1d63d57dd60` (the exact base is recorded by Git;
Runtime 0.2.19). It does not change an app/provider ABI, packaged application,
radio, battery broadcast, storage format, Clock policy, or PMU power-off timer.

Build with `crown-build.ini`, `pio run -c crown-build.ini -e
crown-recovery-qualification -j 1`. The explicit board selection is AXP2101,
physical I2C0, SDA10, SCL11. Generic Runtime defaults remain disabled. The
release integrator must preserve these flags in the final source-bound build.

The native core observes PMU key IRQ history from a high-priority FreeRTOS task
on core0, independently of app callbacks, grants and the foreground loop. An
observed uninterrupted hold requests `esp_restart_noos` after 5000 ms, once.
This bypasses app cleanup and shutdown hooks. A two-word RTC marker produces
`RTE_RECOVERY reset=crown-held-5000ms cleanup=bypassed` on the following software
reset. The task registers with and feeds the SDK task watchdog; registration or
feed failure immediately requests restart. The selected SDK has a five-second
panic-enabled task watchdog and a 300-ms interrupt watchdog on both cores.
These are watchdog configuration facts, not hardware-tested timing claims.

Release (including an ambiguous pair of coalesced edges), failed I2C sampling,
or a sampling gap over 250 ms cancels accumulated hold time. PMU long-press
status alone starts a fresh five-second observation. Silence at boot does not
mean held. No synthetic 30-ms press acceptance is needed: release bounce
cancels the entire five-second interval. PMU IRQ enable bits are preserved except
for the four key sources, which the core requires. Application reads/ACKs have
a separate legacy event history; bulk writes cannot ACK native observations.

Sleep admission drains the sampler under the shared bus mutex. Wake resumes
sampling and consumes the release belonging to a falling-edge GPIO wake, while
still monitoring a held wake key. All normal bus transfers serialize with the
sampler. Other I2C controllers bypass the broker.

## Qualification and limits

`bash test/run_crown_recovery_test.sh` compiles both the policy and the actual
native task implementation, replacing SDK calls with test shims. Tests cover
threshold, releases below/at threshold, bounce, coalesced edges, clock wrap,
long-at-boot observation, bus failure, sleep pause, GPIO-wake release suppression,
legacy/native ACK isolation, missing task/wrong chip, watchdog registration/feed,
reset marker consumption and one restart while a separate foreground thread
spins without yielding. UBSan is supported through `SANITIZE=1
SANITIZERS=undefined`. Target compilation is separate evidence.

This checkpoint is **not yet a host-owned short-press exit implementation** and
is not a firmware delivery or hardware qualification. Monitoring starts only
when the validated PMU physical bus is opened and pauses when that controller
closes or sleeps. The boot-before-open and deep-sleep intervals are not observed.
A press already held at boot cannot be backdated: the first reliable event
starts a new five-second interval; the PMU's existing hardware power-off timer
may win first. Emulator tests do not establish PMU electrical timing, light-sleep
wake timing, scheduler latency, physical radio behavior, or actual watchdog reset
behavior. Unbounded interrupt masking, scheduler/I2C/PMU failure, or corruption by
trusted native/ELF code can defeat software observation. This is not a security
boundary or an impossible-to-override guarantee. No hardware was flashed.
