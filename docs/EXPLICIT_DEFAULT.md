# Explicit configured-default handoff

Runtime 0.1.77 appends `request_default()` after `stream_client` in the v1 table.
Check `RISC_RUNTIME_DEFAULT_REQUEST_V1_SIZE` before accessing it. Every earlier
member and the complete canonical `T5FileOpenApi.h` remain unchanged.

The active owner may call only during `app_main`, with clean native/provider
custody and no pending request. The callback queues the configured default;
it accepts no path and grants no capability. Return immediately after success.
Calls during init/fini, on a foreign task, outside an invocation, while retained,
or after another request fail. A requested file launch cannot be replaced.

An admitted file receiver can explicitly request Home. After its entry, fini,
automatic grant revocation and ELF unload complete, the runtime discards the
file-open session and starts a fresh default invocation. The receiver cannot use
the old `request_launch` callback to bypass the caller. Ordinary receiver return
and load/init failures still return to the fresh caller. Retention at any exit
barrier prevents both caller/default handoff and teardown. Default load/init
failure enters the existing error path; it cannot silently restart the caller.

`bash test/run_default_request_test.sh` exercises 13 cases with real mapped app
ELFs: ordinary/receiver Home, explicit release/automatic revocation, default as
caller/receiver, conflicting requests, entry/fini/explicit/provider retention,
and default load/init failures. All scenarios also check init/fini, foreign-task,
native-unsafe and post-invocation refusal. Run with `SANITIZE=1` for ASan/UBSan.
The existing file-open and runtime suites cover unchanged handoffs; the legacy
header probe asserts each prefix and the new append-only boundary.

These are software tests. No physical device or product cohort is qualified by
the Runtime version alone.
