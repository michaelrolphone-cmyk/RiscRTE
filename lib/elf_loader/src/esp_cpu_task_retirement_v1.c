#include "private/esp_cpu_task_retirement_v1.h"
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_freertos_hooks.h"
#include "freertos/idf_additions.h"

/* This initial observer is pinned to the measured IDF non-SMP kernel. Its
 * actual idle loop calls esp_vApplicationIdleHook AFTER the complete
 * prvCheckTasksWaitingTermination/prvDeleteTCB/newlib/stack/TCB cleanup.
 */
#if CONFIG_FREERTOS_SMP || !INCLUDE_xTaskGetIdleTaskHandle
#error "Requalify the actual kernel cleanup/idle boundary for this profile"
#endif

typedef enum { INSTALL_FRESH, INSTALLING, INSTALLED, INSTALL_RETAINED } install_state_t;
typedef struct {
    uint64_t id;
    TaskHandle_t creator;
    uint32_t budget, reserved;
    bool closed;
} scope_t;
typedef struct {
    esp_cpu_retirement_ticket_t token;
    esp_cpu_retirement_phase_t phase;
    TaskHandle_t identity, direct_caller, cleanup_caller;
    BaseType_t cleanup_core;
    bool cleanup_in_idle, direct_inflight, tls_returned, tail_complete;
} ticket_t;

/* Tiny resident control metadata is explicitly internal DRAM, independent of
 * a product's external-.bss/session-allocation choices. Not a cache-off claim. */
static DRAM_ATTR portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR install_state_t installed;
static DRAM_ATTR scope_t scopes[ESP_CPU_RETIREMENT_SCOPE_LIMIT];
static DRAM_ATTR ticket_t tickets[ESP_CPU_RETIREMENT_TICKET_LIMIT];
static DRAM_ATTR uint64_t next_scope = 1, next_ticket = 1;
static DRAM_ATTR _Atomic bool ack_active[CONFIG_FREERTOS_NUMBER_OF_CORES];

static bool task_context(void) {
    return !xPortInIsrContext() && xTaskGetCurrentTaskHandle() != NULL;
}
static scope_t *scope_find(uint64_t id) {
    if (!id) return NULL;
    for (unsigned i = 0; i < ESP_CPU_RETIREMENT_SCOPE_LIMIT; ++i)
        if (scopes[i].id == id) return &scopes[i];
    return NULL;
}
static ticket_t *ticket_find(esp_cpu_retirement_ticket_t token) {
    if (!token.scope || !token.serial) return NULL;
    for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i)
        if (tickets[i].token.scope == token.scope && tickets[i].token.serial == token.serial)
            return &tickets[i];
    return NULL;
}
/* Only immutable identity comparison. Never inspect/dereference a target TCB. */
static bool actual_idle(TaskHandle_t current, BaseType_t *core_out) {
    const BaseType_t core = xPortGetCoreID();
    if (core < 0 || core >= CONFIG_FREERTOS_NUMBER_OF_CORES) return false;
    const TaskHandle_t idle = xTaskGetIdleTaskHandleForCore(core);
    if (!idle || current != idle) return false;
    if (core_out) *core_out = core;
    return true;
}
static bool any_idle_identity(TaskHandle_t task) {
    for (BaseType_t core = 0; core < CONFIG_FREERTOS_NUMBER_OF_CORES; ++core)
        if (task == xTaskGetIdleTaskHandleForCore(core)) return true;
    return false;
}
static bool scope_caller(scope_t *scope, TaskHandle_t current) {
    if (!scope || scope->closed) return false;
    if (scope->creator == current) return true;
    for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
        const ticket_t *t = &tickets[i];
        if (t->token.scope == scope->id && t->identity == current &&
            t->phase == ESP_CPU_RETIREMENT_REGISTERED) return true;
    }
    return false;
}
static esp_cpu_retirement_result_t fault(ticket_t *t) {
    t->phase = ESP_CPU_RETIREMENT_FAULTED;
    t->tail_complete = false;
    return ESP_CPU_RETIREMENT_RETAINED;
}
static void acknowledge(ticket_t *t) {
    t->tail_complete = true;
    t->phase = ESP_CPU_RETIREMENT_ACKNOWLEDGED;
}
/* Permanent resident callback: no allocation, waiting, target dereference or
 * private callback invocation. The exact calling kernel site supplies the
 * after-tail barrier; an ordinary task invoking this callback cannot ACK.
 */
static bool idle_ack(void) {
    if (!task_context()) return true;
    BaseType_t entry_core = -1;
    const TaskHandle_t entry_task = xTaskGetCurrentTaskHandle();
    if (!actual_idle(entry_task, &entry_core)) return true;
    if (atomic_exchange_explicit(&ack_active[entry_core], true, memory_order_acquire)) return true;
    portENTER_CRITICAL(&lock);
    BaseType_t core = -1;
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (installed == INSTALLED && current == entry_task && actual_idle(current, &core) && core == entry_core) {
        for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
            ticket_t *t = &tickets[i];
            if (t->phase == ESP_CPU_RETIREMENT_TLS_RETURNED && t->tls_returned &&
                t->cleanup_in_idle && t->cleanup_core == core &&
                t->cleanup_caller == current) acknowledge(t);
        }
    }
    portEXIT_CRITICAL(&lock);
    atomic_store_explicit(&ack_active[entry_core], false, memory_order_release);
    return true;
}

esp_cpu_retirement_result_t esp_cpu_retirement_install(void) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    if (installed != INSTALL_FRESH) {
        const esp_cpu_retirement_result_t r = installed == INSTALLED ? ESP_CPU_RETIREMENT_OK :
            installed == INSTALLING ? ESP_CPU_RETIREMENT_PENDING : ESP_CPU_RETIREMENT_RETAINED;
        portEXIT_CRITICAL(&lock);
        return r;
    }
    for (BaseType_t core = 0; core < CONFIG_FREERTOS_NUMBER_OF_CORES; ++core) {
        if (!xTaskGetIdleTaskHandleForCore(core)) {
            portEXIT_CRITICAL(&lock);
            return ESP_CPU_RETIREMENT_CONTEXT;
        }
    }
    installed = INSTALLING;
    portEXIT_CRITICAL(&lock);
    for (UBaseType_t core = 0; core < CONFIG_FREERTOS_NUMBER_OF_CORES; ++core) {
        if (esp_register_freertos_idle_hook_for_cpu(idle_ack, core) != ESP_OK) {
            portENTER_CRITICAL(&lock);
            installed = INSTALL_RETAINED;
            portEXIT_CRITICAL(&lock);
            return ESP_CPU_RETIREMENT_RETAINED;
        }
    }
    portENTER_CRITICAL(&lock);
    installed = INSTALLED;
    portEXIT_CRITICAL(&lock);
    return ESP_CPU_RETIREMENT_OK;
}

esp_cpu_retirement_result_t esp_cpu_retirement_scope_open(uint32_t budget, esp_cpu_retirement_scope_t *out) {
    if (!out || !budget || budget > ESP_CPU_RETIREMENT_TICKET_LIMIT) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_EXHAUSTED;
    if (installed != INSTALLED) r = ESP_CPU_RETIREMENT_RETAINED;
    else if (any_idle_identity(xTaskGetCurrentTaskHandle())) r = ESP_CPU_RETIREMENT_CONTEXT;
    else if (next_scope != UINT64_MAX) {
        for (unsigned i = 0; i < ESP_CPU_RETIREMENT_SCOPE_LIMIT; ++i) {
            if (!scopes[i].id) {
                scopes[i] = (scope_t){next_scope++, xTaskGetCurrentTaskHandle(), budget, 0, false};
                *out = (esp_cpu_retirement_scope_t){scopes[i].id};
                r = ESP_CPU_RETIREMENT_OK;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_scope_close(esp_cpu_retirement_scope_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    scope_t *scope = scope_find(token.id);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (scope && !scope->closed) {
        if (scope->creator != xTaskGetCurrentTaskHandle()) r = ESP_CPU_RETIREMENT_CONTEXT;
        else {
            r = ESP_CPU_RETIREMENT_OK;
            for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
                const ticket_t *t = &tickets[i];
                if (t->token.scope != scope->id) continue;
                if (t->phase == ESP_CPU_RETIREMENT_FAULTED) { r = ESP_CPU_RETIREMENT_RETAINED; break; }
                if (t->direct_inflight || (t->phase != ESP_CPU_RETIREMENT_ACKNOWLEDGED &&
                    t->phase != ESP_CPU_RETIREMENT_ABANDONED)) r = ESP_CPU_RETIREMENT_PENDING;
            }
            if (r == ESP_CPU_RETIREMENT_OK) {
                scope->closed = true;
                scope->creator = NULL;
            }
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_reserve(esp_cpu_retirement_scope_t token, esp_cpu_retirement_ticket_t *out) {
    if (!out) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    scope_t *scope = scope_find(token.id);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (scope && !scope->closed) {
        if (!scope_caller(scope, xTaskGetCurrentTaskHandle())) r = ESP_CPU_RETIREMENT_CONTEXT;
        else if (scope->reserved == scope->budget || next_ticket == UINT64_MAX) r = ESP_CPU_RETIREMENT_EXHAUSTED;
        else {
            r = ESP_CPU_RETIREMENT_EXHAUSTED;
            for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
                if (!tickets[i].token.serial) {
                    tickets[i].token = (esp_cpu_retirement_ticket_t){scope->id, next_ticket++};
                    tickets[i].phase = ESP_CPU_RETIREMENT_RESERVED;
                    tickets[i].cleanup_core = -1;
                    ++scope->reserved;
                    *out = tickets[i].token;
                    r = ESP_CPU_RETIREMENT_OK;
                    break;
                }
            }
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_register_current(esp_cpu_retirement_ticket_t token, TaskHandle_t *out) {
    if (!out) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    scope_t *scope = scope_find(token.scope);
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t && scope && !scope->closed) {
        if (t->phase != ESP_CPU_RETIREMENT_RESERVED) r = ESP_CPU_RETIREMENT_STALE;
        else if (current == scope->creator || any_idle_identity(current)) r = ESP_CPU_RETIREMENT_CONTEXT;
        else {
            r = ESP_CPU_RETIREMENT_OK;
            for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i)
                if (tickets[i].identity == current) { r = ESP_CPU_RETIREMENT_RETAINED; break; }
            if (r == ESP_CPU_RETIREMENT_OK) {
                t->identity = current;
                t->phase = ESP_CPU_RETIREMENT_REGISTERED;
                *out = current;
            } else {
                /* The newly created resident trampoline must retire itself
                 * without private entry. Never reassign the old tombstone. */
                fault(t);
            }
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_abandon(esp_cpu_retirement_ticket_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t) {
        if (!scope_caller(scope_find(token.scope), xTaskGetCurrentTaskHandle())) r = ESP_CPU_RETIREMENT_CONTEXT;
        else if (t->phase == ESP_CPU_RETIREMENT_RESERVED && !t->identity) {
            t->phase = ESP_CPU_RETIREMENT_ABANDONED;
            r = ESP_CPU_RETIREMENT_OK;
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_lookup(esp_cpu_retirement_scope_t token, TaskHandle_t identity,
                                                     esp_cpu_retirement_ticket_t *out) {
    if (!identity || !out) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    scope_t *scope = scope_find(token.id);
    if (scope && !scope->closed && !scope_caller(scope, xTaskGetCurrentTaskHandle())) r = ESP_CPU_RETIREMENT_CONTEXT;
    else if (scope && !scope->closed) {
        for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
            ticket_t *t = &tickets[i];
            if (t->token.scope == scope->id && t->identity == identity &&
                t->phase == ESP_CPU_RETIREMENT_REGISTERED && !t->direct_inflight) {
                *out = t->token;
                r = ESP_CPU_RETIREMENT_OK;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_direct_begin(esp_cpu_retirement_ticket_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t) {
        const scope_t *scope = scope_find(token.scope);
        if (!scope || scope->closed) r = ESP_CPU_RETIREMENT_STALE;
        else if (!scope_caller(scope_find(token.scope), current) || any_idle_identity(current) || current == t->identity)
            r = ESP_CPU_RETIREMENT_CONTEXT;
        else if (t->phase == ESP_CPU_RETIREMENT_REGISTERED && !t->direct_inflight) {
            t->direct_caller = current;
            t->direct_inflight = true;
            r = ESP_CPU_RETIREMENT_OK;
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_cleanup_enter(esp_cpu_retirement_ticket_t token, TaskHandle_t target) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t && t->identity == target && target && t->phase == ESP_CPU_RETIREMENT_REGISTERED) {
        const TaskHandle_t current = xTaskGetCurrentTaskHandle();
        BaseType_t core = -1;
        const bool idle = actual_idle(current, &core);
        if (current == target || (!idle && (!t->direct_inflight || current != t->direct_caller)))
            r = ESP_CPU_RETIREMENT_CONTEXT;
        else {
            t->cleanup_caller = current;
            t->cleanup_in_idle = idle;
            t->cleanup_core = core;
            t->phase = ESP_CPU_RETIREMENT_CLEANUP_ENTERED;
            r = ESP_CPU_RETIREMENT_OK;
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_cleanup_returned(esp_cpu_retirement_ticket_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t && t->phase == ESP_CPU_RETIREMENT_CLEANUP_ENTERED) {
        const TaskHandle_t current = xTaskGetCurrentTaskHandle();
        BaseType_t core = -1;
        if (current != t->cleanup_caller || (t->cleanup_in_idle &&
            (!actual_idle(current, &core) || core != t->cleanup_core))) r = ESP_CPU_RETIREMENT_CONTEXT;
        else {
            t->tls_returned = true;
            t->phase = ESP_CPU_RETIREMENT_TLS_RETURNED;
            r = ESP_CPU_RETIREMENT_OK;
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_direct_returned(esp_cpu_retirement_ticket_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t) {
        if (!t->direct_inflight || t->direct_caller != xTaskGetCurrentTaskHandle()) r = ESP_CPU_RETIREMENT_CONTEXT;
        else {
            t->direct_inflight = false;
            if (t->phase == ESP_CPU_RETIREMENT_ACKNOWLEDGED) r = ESP_CPU_RETIREMENT_OK;
            else if (t->phase == ESP_CPU_RETIREMENT_FAULTED) r = ESP_CPU_RETIREMENT_RETAINED;
            else if (t->phase == ESP_CPU_RETIREMENT_TLS_RETURNED && !t->cleanup_in_idle &&
                     t->cleanup_caller == t->direct_caller) {
                acknowledge(t);
                r = ESP_CPU_RETIREMENT_OK;
            } else if (t->phase == ESP_CPU_RETIREMENT_CLEANUP_ENTERED && !t->cleanup_in_idle)
                r = fault(t); /* impossible successful synchronous-return ordering */
            else r = ESP_CPU_RETIREMENT_PENDING; /* running other-core task; idle must finish */
        }
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_poll(esp_cpu_retirement_ticket_t token, esp_cpu_retirement_report_t *out) {
    if (!out) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t) {
        *out = (esp_cpu_retirement_report_t){t->phase, t->identity != NULL, t->direct_inflight,
            t->tls_returned, t->tail_complete, t->tail_complete, t->phase == ESP_CPU_RETIREMENT_FAULTED};
        r = (t->phase == ESP_CPU_RETIREMENT_ACKNOWLEDGED && !t->direct_inflight) || t->phase == ESP_CPU_RETIREMENT_ABANDONED ?
            ESP_CPU_RETIREMENT_OK : t->phase == ESP_CPU_RETIREMENT_FAULTED ?
            ESP_CPU_RETIREMENT_RETAINED : ESP_CPU_RETIREMENT_PENDING;
    }
    portEXIT_CRITICAL(&lock);
    return r;
}

esp_cpu_retirement_result_t esp_cpu_retirement_retain(esp_cpu_retirement_ticket_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&lock);
    ticket_t *t = ticket_find(token);
    esp_cpu_retirement_result_t r = ESP_CPU_RETIREMENT_STALE;
    if (t) {
        if (!scope_caller(scope_find(token.scope), xTaskGetCurrentTaskHandle())) r = ESP_CPU_RETIREMENT_CONTEXT;
        else if (t->phase == ESP_CPU_RETIREMENT_ACKNOWLEDGED || t->phase == ESP_CPU_RETIREMENT_ABANDONED)
            r = ESP_CPU_RETIREMENT_STALE;
        else r = fault(t);
    }
    portEXIT_CRITICAL(&lock);
    return r;
}
