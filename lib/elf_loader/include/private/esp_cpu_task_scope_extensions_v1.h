#pragma once
#include "private/esp_cpu_task_scope_v1.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Resident-only facade frame. No provider import, pointer-finalizer or second
 * task registry. Stack-owned, initialize to zero, enter/leave exactly once.
 * Deletion may close new admission while an entered frame finishes. */
typedef struct {
    esp_cpu_task_scope_t scope;
    esp_cpu_scoped_task_t caller;
    TaskHandle_t identity;
    bool active;
} esp_cpu_task_scope_frame_v1;
esp_cpu_retirement_result_t esp_cpu_task_scope_frame_enter_v1(
    esp_cpu_task_scope_t, esp_cpu_task_scope_frame_v1 *);
esp_cpu_retirement_result_t esp_cpu_task_scope_frame_leave_v1(esp_cpu_task_scope_frame_v1 *);
/* Two bounded plain-data outputs committed under the deletion lock, immediately
 * before frame release. No callback and no publication after dropping custody.
 * Each nonzero copy is <=64 bytes; outputs unchanged on refusal. */
esp_cpu_retirement_result_t esp_cpu_task_scope_frame_publish_v1(
    esp_cpu_task_scope_frame_v1 *, void *, const void *, size_t,
    void *, const void *, size_t);
/* Resident-only two-phase publication. Create registers the real task and TLS
 * proxy but leaves its existing go gate closed. The returned identity is known
 * only to the admitted resident creator until frame_publish_child commits both
 * bounded converted outputs and go under the same lock. No private callback.
 * On uncertain publication retain; exact held cleanup still needs full tail. */
esp_cpu_retirement_result_t esp_cpu_task_scope_create_held_v1(
    esp_cpu_task_scope_t, const esp_cpu_scoped_task_config_t *,
    esp_cpu_scoped_task_publication_t *);
esp_cpu_retirement_result_t esp_cpu_task_scope_frame_publish_child_v1(
    esp_cpu_task_scope_frame_v1 *, esp_cpu_scoped_task_t,
    void *, const void *, size_t, void *, const void *, size_t);
/* Irreversible child-creation fence, not scope close/retirement. Only actual
 * creator or the exact designated owner child may set it. */
esp_cpu_retirement_result_t esp_cpu_task_scope_seal_v1(
    esp_cpu_task_scope_t, esp_cpu_scoped_task_t owner);
/* Requires the creation fence. OK means all OTHER tickets ACK/abandoned and
 * no other resident operation in flight, not that owner or image is retired. */
esp_cpu_retirement_result_t esp_cpu_task_scope_peers_retired_v1(
    esp_cpu_task_scope_t, esp_cpu_scoped_task_t owner);
esp_cpu_retirement_result_t esp_cpu_task_scope_retain_v1(esp_cpu_task_scope_t);
#ifdef __cplusplus
}
#endif
