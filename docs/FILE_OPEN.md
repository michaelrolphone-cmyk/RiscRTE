# Deployment-admitted file dispatch

The optional ordinary app service `file.open@1`, instance `0`, uses the unchanged
Reader `T5FileOpenApi.h` table. Apps obtain it through
`risc_runtime_get_api(1)->acquire("file.open", 1, 0, &grant)`, check its version and
size, and release the grant before returning. Both the declaring manifest and
owner-supplied boot policy must authorize this capability. The standalone
`t5_file_open_get_api` declaration is retained for source ABI custody; it is not
added to the ELF import allowlist. No privileged package or direct file API is
introduced.

## Immutable handler metadata

Only application manifests selected by `boot.json.app_capabilities` contribute
handlers. An unselected JSON file, filesystem entry, SD executable, or app-supplied
name cannot become a handler. A selected manifest can add:

```json
{
  "display_name": "Text Viewer",
  "icon": "text",
  "supported_file_types": [".txt", ".md"]
}
```

There are at most 12 distinct extensions per app. Each is a leading dot plus
1–14 lowercase ASCII letters or digits. Explicit null, wrong types, duplicates,
uppercase declarations, invalid characters and oversized fields reject admission.
Handler IDs are at most 63 bytes; display names at most 95; icons at most 23.
Missing display names use the app ID; missing icons are empty. Names are copied
into runtime-owned metadata before the JSON document is destroyed. Only apps with
a nonempty list need bounded handler metadata storage; allocation follows the
runtime's existing selected RAM/PSRAM metadata policy.

Every returned handler is `T5_FILE_HANDLER_APP` (kind `0`). There is no built-in
reader or other system handler. Order follows the explicit boot policy; multiple
matching apps remain separate choices. Extension lookup is ASCII case-insensitive.
The declaration is metadata only: it grants neither storage access nor file-open
authority. A receiving app needs its own explicit `file.open@1` grant to read its
incoming source path, and separate storage grants to access the content.

`refresh()` checks live authority and confirms the immutable in-memory snapshot.
It does not scan storage, rewrite a registry, or adopt changed on-disk JSON.
Associations change only with a newly admitted boot session. App-update admission
validates the same bounded metadata schema without replacing current-session
metadata.

## Data path and handoff

Query and open paths must be absolute `/sd/...` paths, valid UTF-8 and at most
511 bytes plus NUL (the canonical 512-byte buffer). Empty, dot and dot-dot
components, repeated/trailing slashes, backslashes, control bytes, invalid UTF-8
and paths outside `/sd/` reject. The final extension must match the selected
handler's declaration. No path normalization silently changes the request.

`open_request(path, app_id, cookie)` copies the path, 64-bit cookie and bounded
policy indices. The target ELF is exclusively the exact executable already
bound to that handler's immutable boot-policy identity. An SD data path is never
stripped to a launch name or used as executable input. The service does no SD
I/O and does not assert that a source exists; content/open errors belong to the
receiving app and its granted storage provider.

Requests are accepted only during the caller's `app_main`, with no prior queued
launch and no active handoff/result. The caller releases resources and grants,
then returns. It is finalized and unmapped before a fresh receiver invocation
starts. `source_path_get` copies the original path only for that receiver while
it holds a live file-open grant, including its initialization callback. It rejects
short buffers and clears the supplied output on failure. Releasing the grant
revokes access without canceling the already-copied handoff.

The receiver's return, missing/invalid ELF, or failed initialization reloads the
original caller afresh, even if that caller is not the configured default or the
receiver is the configured default. `open_take_result` is caller-only and
one-shot: `0` means the receiver invocation completed; `-1` means admission/load
or initialization/entry validation failed. These are `ESP_OK`/`ESP_FAIL` values,
not content-processing results. The exact cookie accompanies either result.
Result output pointers are optional. No callbacks, loaded pointers or app static
state are retained. Unconsumed results are discarded when the resumed caller
exits; failed caller reload returns to normal default/error handling.

Nested file opens, self-handoffs and ordinary launch requests during an active
handoff reject. After the result is consumed, the caller may request another
handoff. Native/provider/stream retention still prevents teardown and relaunch;
all file-open state is revoked on terminal session exit or a retention barrier.
Capability calls outside the owner task, after the last grant is released, or
outside an active admitted invocation fail closed. The canonical context-free
table acts only under the current invocation's live grant; native code remains
trusted rather than memory-isolated.

## Verification and provenance

Run `bash test/run_file_open_test.sh`; `SANITIZE=1` enables ASan/UBSan. Host shared
modules test immutable handler copies, exact global grants, stale grant rejection,
SD path bounds and malformed inputs, copied source/ID buffers, unload-before-open,
fresh caller/default combinations, load/init failures, one-shot/discarded results,
and failed caller/receiver retention. The suite pins the canonical header hash,
places unterminated path/ID bounds against protected pages, checks exact-capacity
output canaries, and repeats successful and failed opens from freshly reloaded
callers. Invalid images, missing entry points, failed init with an unreleased
grant, automatic grant cleanup, and optional result outputs exercise the actual
Runtime implementation. These tests do not qualify hardware.

`sdk/app/T5FileOpenApi.h` is byte-exact from
`michaelrolphone-cmyk/T5S3-Reader` commit
`34d8e694d89a1e72d8854403d8592c289fae3ddc`, path
`lib/NativeApps/include/T5FileOpenApi.h`, SHA-256
`c1b873e364dba81ce39d8c8a62668db618735407a6291dca87b9795ce4accc30`.
The dispatch integration and focused tests are new headless-runtime code.
Reader's registry writer, app store, activity/UI machinery and built-in system
reader were not imported. The repository's preserved MIT licensing applies.
