#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resident-only observer, NOT an ELF import or public task deletion API.
 * No function here creates/deletes a task or invokes provider callbacks.
 * The future resident creation/deletion/TLS wrappers are the only permitted
 * writers. The existing kernel's after-reclamation idle call site is required.
 */
#define ESP_CPU_RETIREMENT_SCOPE_LIMIT 4u
#define ESP_CPU_RETIREMENT_TICKET_LIMIT 32u
typedef struct { uint64_t id; } esp_cpu_retirement_scope_t;
typedef struct { uint64_t scope; uint64_t serial; } esp_cpu_retirement_ticket_t;
typedef enum {
    ESP_CPU_RETIREMENT_OK = 0,
    ESP_CPU_RETIREMENT_PENDING = 1,
    ESP_CPU_RETIREMENT_INVALID = -1,
    ESP_CPU_RETIREMENT_CONTEXT = -2,
    ESP_CPU_RETIREMENT_EXHAUSTED = -3,
    ESP_CPU_RETIREMENT_STALE = -4,
    ESP_CPU_RETIREMENT_RETAINED = -5
} esp_cpu_retirement_result_t;
typedef enum {
    ESP_CPU_RETIREMENT_RESERVED,
    ESP_CPU_RETIREMENT_REGISTERED,
    ESP_CPU_RETIREMENT_CLEANUP_ENTERED,
    ESP_CPU_RETIREMENT_TLS_RETURNED,
    ESP_CPU_RETIREMENT_ACKNOWLEDGED,
    ESP_CPU_RETIREMENT_ABANDONED,
    ESP_CPU_RETIREMENT_FAULTED
} esp_cpu_retirement_phase_t;
typedef struct {
    esp_cpu_retirement_phase_t phase;
    bool identity_registered;
    bool direct_delete_inflight;
    bool tls_returned;
    bool kernel_tail_complete;
    /* Complete kernel cleanup retired task ownership, not necessarily storage:
     * user-supplied static stack/TCB memory remains allocated. The future create
     * wrapper owns allocation-kind knowledge; a raw handle cannot supply it. */
    bool kernel_task_ownership_retired;
    bool retained;
} esp_cpu_retirement_report_t;

/* Registers permanent resident hooks on all actual cores before any scope can
 * open. A partially failed registration is permanently retained, not undone
 * while a fetched hook may exist. Missing idle handles before registration
 * are a clean context refusal. No kernel configuration is changed.
 */
esp_cpu_retirement_result_t esp_cpu_retirement_install(void);
/* budget is the total lifetime creation budget, not concurrent task count.
 * The resident creator worker must remain alive/pinned until scope_close succeeds;
 * this observer does not turn its raw identity into a new authority.
 * Fixed resident metadata/tombstones deliberately survive to boot end. Exhaustion
 * refuses new creation BEFORE a task is created. No active task/TCB is leaked
 * by an ACK: only the actual complete-kernel-cleanup boundary can produce it.
 */
esp_cpu_retirement_result_t esp_cpu_retirement_scope_open(
    uint32_t budget, esp_cpu_retirement_scope_t *out);
/* Trusted creator only. Succeeds only after every reserved ticket is ACK or
 * clean-abandoned and no direct deletion transaction remains. Clears creator
 * identity and permanently closes event admission while retaining tombstones.
 * This retires the observer's creator requirement, not physical-owner custody.
 */
esp_cpu_retirement_result_t esp_cpu_retirement_scope_close(esp_cpu_retirement_scope_t scope);
esp_cpu_retirement_result_t esp_cpu_retirement_reserve(
    esp_cpu_retirement_scope_t scope, esp_cpu_retirement_ticket_t *out);
/* Called only from the new resident child trampoline, before private entry.
 * Captures its actual current identity, never a caller-supplied TCB pointer.
 * Must finish before publishing that identity back to the vendor caller.
 */
esp_cpu_retirement_result_t esp_cpu_retirement_register_current(
    esp_cpu_retirement_ticket_t ticket, TaskHandle_t *identity_out);
/* Clean creation failure BEFORE any child/kernel identity was attached. */
esp_cpu_retirement_result_t esp_cpu_retirement_abandon(
    esp_cpu_retirement_ticket_t ticket);
/* Raw identity is only a compatibility key within this exact resident scope.
 * Retired identities are tombstones and cannot select an unrelated reused TCB.
 */
esp_cpu_retirement_result_t esp_cpu_retirement_lookup(
    esp_cpu_retirement_scope_t scope, TaskHandle_t identity,
    esp_cpu_retirement_ticket_t *out);
/* Trusted resident deletion wrapper only: bracket the REAL vTaskDelete call.
 * The returned observer status does NOT authorize entering an unsafe vendor
 * cancellation path. Returning PENDING/RETAINED from a void SDK delete wrapper
 * would be unsafe; that wrapper must wait or permanently retain/park instead.
 */
esp_cpu_retirement_result_t esp_cpu_retirement_direct_begin(
    esp_cpu_retirement_ticket_t ticket);
esp_cpu_retirement_result_t esp_cpu_retirement_direct_returned(
    esp_cpu_retirement_ticket_t ticket);
/* Trusted resident TLS proxy only. Enter while target TCB is still valid;
 * returned only after the existing complete TLS destructor chain returns.
 * Neither event acknowledges the subsequent newlib/TCB cleanup tail.
 */
esp_cpu_retirement_result_t esp_cpu_retirement_cleanup_enter(
    esp_cpu_retirement_ticket_t ticket, TaskHandle_t target_identity);
esp_cpu_retirement_result_t esp_cpu_retirement_cleanup_returned(
    esp_cpu_retirement_ticket_t ticket);
esp_cpu_retirement_result_t esp_cpu_retirement_poll(
    esp_cpu_retirement_ticket_t ticket, esp_cpu_retirement_report_t *out);
/* A terminal custody failure never becomes ACK later, even if another hook runs. */
esp_cpu_retirement_result_t esp_cpu_retirement_retain(
    esp_cpu_retirement_ticket_t ticket);

#ifdef __cplusplus
}
#endif
