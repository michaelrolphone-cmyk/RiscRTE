#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Candidate native OS/CPU revision5 ONLY. Not registered in any inventory yet.
 * Frozen worker-v2 and OS/CPU revisions1..4 are unchanged. Native code remains
 * trusted, not memory-sandboxed. Image-wide callback closure is a separate
 * admission obligation and is NOT certified by task completion. */
typedef uint64_t risc_cpu_worker_v5;
typedef uint64_t risc_cpu_task_scope_v1;
typedef struct { uint64_t scope, serial; } risc_cpu_scoped_task_v1;
enum {
    RISC_CPU_SCOPE_OK = 0, RISC_CPU_SCOPE_PENDING = 1,
    RISC_CPU_SCOPE_INVALID = -1, RISC_CPU_SCOPE_CONTEXT = -2,
    RISC_CPU_SCOPE_LIMIT = -3, RISC_CPU_SCOPE_STALE = -4,
    RISC_CPU_SCOPE_RETAINED = -5
};
typedef struct {
    uint32_t struct_size;
    void (*entry)(void *);
    void *argument;
    /* Initial candidate accepts NULL/zero or mapped writable static storage
     * only. Heap and borrowed arguments are refused; no ownership guess. */
    uint32_t argument_bytes;
    uint32_t stack_bytes, working_stack_bytes, priority;
    int32_t core;
    char name[16];
} risc_cpu_task_config_v1;
typedef struct {
    risc_cpu_scoped_task_v1 task;
    uintptr_t identity; /* observation, not deletion authority */
    uint32_t owner; /* logical owner worker, NOT Runtime creator */
} risc_cpu_task_identity_v1;
typedef struct {
    uint32_t tick_period_ms, max_priorities, number_of_cores;
} risc_cpu_task_kernel_v1;

/* Only within the resident Module start window. Success publishes a resident-
 * held task, NOT private entry. Exact Graph grant commit is host-private and
 * occurs after Module start/Graph acquisition. Output must initially be zero
 * and stays unchanged on refusal. This is not a working-stack adequacy proof. */
int risc_cpu_worker_start_v5(const risc_cpu_task_config_v1 *, risc_cpu_worker_v5 *);
/* Creator-only, bounded poll. OK only after owner+children complete kernel tail
 * and actual scope close. It is not image-wide/physical retirement. */
int risc_cpu_worker_join_v5(risc_cpu_worker_v5);
int risc_cpu_worker_release_v5(risc_cpu_worker_v5);
/* Retrieves this authenticated worker's already attached scope; does not open
 * a nested registry/scope. Runtime creator cannot impersonate provider owner. */
int risc_cpu_task_scope_current_v1(risc_cpu_task_scope_v1 *);
int risc_cpu_task_current_v1(risc_cpu_task_scope_v1, risc_cpu_task_identity_v1 *);
int risc_cpu_task_kernel_v1_get(risc_cpu_task_scope_v1, risc_cpu_task_kernel_v1 *);
/* Both outputs must initially be zero. The registered child remains at a
 * resident gate until converted outputs and go are committed under the caller's
 * scope/deletion lock. Uncertain publication retains without releasing entry.
 * Source contract only until actual composed target/tests qualify this successor. */
int risc_cpu_task_create_v1(risc_cpu_task_scope_v1, const risc_cpu_task_config_v1 *,
                            risc_cpu_scoped_task_v1 *, uintptr_t *);
int risc_cpu_task_resolve_v1(risc_cpu_task_scope_v1, uintptr_t, risc_cpu_scoped_task_v1 *);
int risc_cpu_task_delete_join_v1(risc_cpu_task_scope_v1, risc_cpu_scoped_task_v1);
void risc_cpu_task_exit_v1(risc_cpu_task_scope_v1); /* nonreturn */
void risc_cpu_task_retain_v1(risc_cpu_task_scope_v1); /* nonreturn */
int risc_cpu_task_delay_v1(risc_cpu_task_scope_v1, uint32_t ticks);
/* Owner-only, closes new child admission and checks peer tails. OK says peers
 * drained; owner still alive, scope NOT closed and image NOT releasable. */
int risc_cpu_task_scope_request_close_v1(risc_cpu_task_scope_v1);
#ifdef __cplusplus
}
#endif
