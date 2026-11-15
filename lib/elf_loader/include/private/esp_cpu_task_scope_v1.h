#pragma once
#include "private/esp_cpu_task_retirement_v1.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#define ESP_CPU_SCOPE_NORETURN [[noreturn]]
#else
#define ESP_CPU_SCOPE_NORETURN _Noreturn
#endif

/* TRUSTED RESIDENT ONLY. Not a provider import, public ABI, cancellation policy
 * or authority to delete arbitrary tasks. The caller pins the private image and
 * all entry/argument/destructor/FILE state until checked scope_close succeeds.
 * Creator worker must stay alive until then. No app-borrowed state is permitted.
 * These wrappers require the pinned non-SMP dynamic-allocation/newlib/TLS kernel.
 */
typedef esp_cpu_retirement_scope_t esp_cpu_task_scope_t;
typedef esp_cpu_retirement_ticket_t esp_cpu_scoped_task_t;
typedef void (*esp_cpu_scoped_entry_t)(void *);
typedef struct {
    esp_cpu_scoped_entry_t entry;
    void *argument;
    const char *name; /* copied before xTaskCreatePinnedToCore */
    /* Exact pinned ESP-IDF argument, in bytes, not vanilla FreeRTOS words.
     * Caller supplies an adequate stack for its task; zero is rejected and
     * allocation failure remains checked. Type bounds are not adequacy proof. */
    uint32_t stack_bytes;
    /* Required caller-qualified live working demand, excluding initial kernel
     * CPSA/TLS/frame regions. This includes the resident trampoline and any
     * scoped APIs invoked by the task. Nonzero is mandatory, not a proof that
     * the supplied budget is sufficient. Foreign cleanup-executor stacks must
     * be qualified separately before admitting their private callbacks. */
    uint32_t working_stack_bytes;
    uint32_t priority;
    int32_t core; /* tskNO_AFFINITY or an actual core */
} esp_cpu_scoped_task_config_t;
typedef struct {
    esp_cpu_scoped_task_t ticket;
    /* Trusted compatibility observation only. Never accepts this raw identity
     * as deletion authority. Future vendor handle consumers remain unqualified. */
    TaskHandle_t kernel_identity;
} esp_cpu_scoped_task_publication_t;

/* Initial bounded scope creator must be an unscoped pinned owner worker. A
 * scoped child may create children in its scope, but cannot open a nested scope
 * whose creator pin would require a separate hierarchical lifetime contract. */
esp_cpu_retirement_result_t esp_cpu_task_scope_open(
    uint32_t lifetime_budget, esp_cpu_task_scope_t *out);
/* Creator-only; requires observer ACK/clean abandonment and no wrapper operation
 * in flight, then clears callback/data references. Retains identity tombstones.
 */
esp_cpu_retirement_result_t esp_cpu_task_scope_close(esp_cpu_task_scope_t scope);
/* May block waiting for actual child publication or complete setup-failure
 * cleanup. Output unchanged on failure. Partial/uncertain custody is Retained,
 * never converted into a clean allocation failure. There is no forced timeout.
 */
esp_cpu_retirement_result_t esp_cpu_task_scope_create(
    esp_cpu_task_scope_t scope, const esp_cpu_scoped_task_config_t *config,
    esp_cpu_scoped_task_publication_t *out);
esp_cpu_retirement_result_t esp_cpu_task_scope_poll(
    esp_cpu_scoped_task_t task, esp_cpu_retirement_report_t *out);
/* Pre-action validation errors return without deletion. Once delete is admitted,
 * this function returns ONLY after complete observer ACK. Pending waits; Retained
 * parks the caller permanently in resident code. Self-delete never returns.
 * Fresh resident operations are fenced first; already-admitted resident frames
 * must drain before the one actual kernel deletion call. Duplicate joins wait.
 * Checked task retirement does not prove arbitrary vendor cancellation is safe.
 */
esp_cpu_retirement_result_t esp_cpu_task_scope_delete_join(esp_cpu_scoped_task_t task);
/* Future void-OSI-compatible boundary: even a pre-action error parks rather than
 * falsely returning successful void completion. Invalid ISR context fail-stops.
 * Not yet bound to any actual vendor table or exported to a provider.
 */
void esp_cpu_task_scope_delete_or_park(esp_cpu_scoped_task_t task);
ESP_CPU_SCOPE_NORETURN void esp_cpu_task_scope_exit_current(esp_cpu_task_scope_t scope);

/* Resident facade accessors. Same existing registry and caller admission as
 * create/delete, never a second raw-handle authority. Output stays unchanged on
 * refusal; all output writes are inside the deletion-serialization lock.
 * creator means the actual scope creator, not a provider's logical owner; a v5
 * facade must map its authenticated owner ticket separately within attachment.
 */
typedef struct {
    TaskHandle_t identity;
    esp_cpu_scoped_task_t ticket; /* zero only for actual scope creator */
    bool creator;
} esp_cpu_task_scope_current_v1_t;
esp_cpu_retirement_result_t esp_cpu_task_scope_current_v1(
    esp_cpu_task_scope_t scope, esp_cpu_task_scope_current_v1_t *out);
/* Exact registered child or its immutable tombstone. Does not dereference TCB
 * storage or admit deletion. Current caller must still be the live creator or
 * an admitted running child. Creator identity never resolves as a child. */
esp_cpu_retirement_result_t esp_cpu_task_scope_resolve_v1(
    esp_cpu_task_scope_t scope, TaskHandle_t identity, esp_cpu_scoped_task_t *out);

#undef ESP_CPU_SCOPE_NORETURN
#ifdef __cplusplus
}
#endif
