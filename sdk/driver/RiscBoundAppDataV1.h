#ifndef RISC_BOUND_APP_DATA_V1_H
#define RISC_BOUND_APP_DATA_V1_H
#include <RiscAppDataV1.h>
#define RISC_BOUND_APP_DATA_CAPABILITY "storage.app-data.bound"
#define RISC_BOUND_APP_DATA_API_V1 1u
#define RISC_BOUND_APP_DATA_BINDINGS_MAX 4u
/* Provider-only complete-file view. The exact boot driver selection authorizes
 * 1..4 distinct {name,namespace,access} bindings. Each filename is forwarded
 * unchanged into its bound namespace; the caller cannot choose a namespace.
 * Uses RiscAppDataV1's copied stat/read/atomic replace layout, limits, revisions,
 * output and status contract. Unlisted names and read-only writes return
 * CONTEXT without backend I/O. Invalid names/arguments return INVALID.
 * Calls require this provider's admitted start/active lifetime on the Runtime
 * owner task, but do not require an active foreground app. Authority is revoked
 * before failed-start diagnostics, quiesce and stop. Copied callbacks cannot
 * revive after release, revocation or Runtime replacement. RETAINED propagates
 * unchanged and permanently fences this invocation's I/O and provider teardown
 * until restart, even if a native safety flag subsequently clears.
 * Binding admission is metadata-only. Storage capacity and quotas are backend
 * mechanics, never application schemas or record counts. No enumerate/delete,
 * wildcard, alias, path, format, migration or default-data authority is added.
 */
typedef risc_app_data_v1 risc_bound_app_data_v1;
#endif
