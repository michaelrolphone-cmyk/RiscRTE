# Terminal invocation retention (Runtime 0.1.51)

Source base: `7e79a8f06c1ee3b71214408c2477d9aaca985834` (0.1.50).
Version 0.1.51 was reserved after checking live branches/tags, open PRs, and
issue/PR/comment claims. The test-only characterization is preserved in
`ef9aa35`; see [before-fix evidence](../test/PROVIDER_EXIT_CHARACTERIZATION.md).

## Two different sources of uncertainty

The app exit barrier now also checks the existing nonmutating
`GraphV2::activationSafe()`. A Failed module or pending-release grant prevents
fini, automatic grant revocation, allocation reclamation, unload and subsequent
launch. This includes failed demand acquisition that never produced an app
grant. No provider cleanup is called merely to inspect that state. The same
barrier runs before loading, after init, after app_main, and after fini.

A false acquisition that completely rolled back remains ordinary failure.
An explicit in-app release retry may still recover before returning. Returning
with a pending release is terminal; Runtime no longer silently retries it after
fini. A failure first exposed by automatic grant revocation still retains the
image under the existing revocation path.

A provider can instead remain graph-Active while privately knowing cleanup is
uncertain. Runtime does not interpret opaque capability statuses. In particular,
a boot-session provider reference can defer quiescence beyond app unload.
A bool operation failure is not automatically proof of that terminal state.

## One-way app signal in the existing service

`risc_runtime_api_v1` appends exactly one callback after `confirm_boot`:

```c
bool (*retain_invocation)(void);
```

Check api_version1, struct_size >= `RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE`, and
the callback before using it. The complete previous prefix, versions, capability
policy, grant limits and provider ABI remain unchanged. There is no new
capability, manifest grant, hardware API, provider selector or raw import.

The current invocation's owner may call from init, main or fini when it chooses
to treat capability-local uncertainty as terminal. True means Runtime has:

- Logically disabled the app APIs and all provider-bound storage tokens.
- Discarded queued launch/file handoff authority.
- Preserved invocation allocations, app image, provider mappings, typed
  configuration, dependencies and existing grant custody until restart.

No provider release, quiesce, stop, poll, allocation free, physical cleanup,
sleep or restart occurs. Previously copied direct capability pointers remain
readable native addresses; this is not memory isolation. The app must stop
calling them, skip its remaining cleanup, and return promptly. This signal
cannot undo cleanup already performed before it was called.

Repeating the signal on the same current retained invocation returns true,
including after provider-promotion RETAINED. It never reactivates authority.
Foreign tasks, calls with no current invocation, and reentrant calls during
provider promotion return false without mutation. A saved callback returns
false after Runtime::run has finished. `risc_runtime_get_api(1)` returns null
once retained, so callers should use the table they already checked.

`runtime.retained-wake` is copied persistence for a future classified wake and
cannot provide this fence. Provider promotion is graph activation, not an
alternate way to request terminal retention. An older Runtime without the
suffix cannot guarantee a provider-local fence; applications requiring it must
check compatibility before beginning those operations.

## Verification

`bash test/run_provider_exit_lifecycle_test.sh` covers real Runtime/Graph/dlopen
images, dependency mappings, clean refusal and rollback, explicit release
recovery, automatic pre/post-fini graph fencing, native retention, provider-local
terminal signaling, init/main/fini/child/no-grant signals, owner denial,
idempotence, queued-child suppression and a separately compiled frozen old ABI
consumer. Its GPIO case uses actual CpuPort with only lowest hardware callbacks
modeled; GPIO write false need not poison native state, so explicit provider-local
retention is essential. No X4 panel protocol or hardware success is simulated.

The bound-storage suite also checks immediate signal-time revocation before
app_main returns, zero cleanup/backend calls, retained mappings, and nonrevival
of old contexts across a new Runtime. Promotion regressions check signaling an
already-retained invocation. The frozen historical header proof verifies every
old offset and size plus both append-only callbacks on host and Xtensa.

The focused suites pass with GCC14 and ASan/UBSan. Local LeakSanitizer is disabled
because this executor uses ptrace. Full target firmware verification is delegated
to exact-head CI: the shared local tools include Xtensa GCC but not the pinned
PlatformIO framework/platform, and no additional tools are installed. Host tests
and target builds do not qualify physical sleep, wake, current or GPIO behavior.
