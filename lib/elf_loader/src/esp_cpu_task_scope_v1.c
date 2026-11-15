#include "private/esp_cpu_task_scope_v1.h"
#include "private/esp_cpu_task_scope_extensions_v1.h"
#include "private/esp_cpu_task_stack_v1.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "freertos/idf_additions.h"

/* This is the exact resident pthread registry, not a private image's copy.
 * Verified SDK pthread_local_storage.c clears this target's slot after walking
 * the complete list. Its ordinary setspecific never replaces a non-null list.
 * This internal symbol is deliberately NOT added to any provider inventory. */
extern void pthread_internal_local_storage_destructor_callback(TaskHandle_t handle);

#if CONFIG_FREERTOS_SMP || CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS != 1 || \
    !CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS || !INCLUDE_vTaskDelete || !INCLUDE_vTaskDelay || \
    !configSUPPORT_DYNAMIC_ALLOCATION
#error "Requalify scoped task creation and complete cleanup for this kernel profile"
#endif

/* Pinned IDF xTaskCreatePinnedToCore takes uint32_t BYTES. The selected
 * Xtensa port uses uint8_t StackType_t, so the kernel's sizeof(StackType_t)
 * multiplier is one. Reject other port/type profiles at compile time rather
 * than silently reinterpreting bytes as words or narrowing to size_t. */
_Static_assert(CHAR_BIT == 8 && sizeof(StackType_t) == 1,
               "Requalify non-byte FreeRTOS stack port");
_Static_assert(SIZE_MAX >= UINT32_MAX,
               "ESP-IDF uint32_t stack bytes must be representable by size_t");

typedef enum { SLOT_UNUSED, SLOT_CREATING, SLOT_READY, SLOT_RUNNING,
               SLOT_SELF_EXIT, SLOT_DELETE_WAIT, SLOT_DIRECT_EXIT, SLOT_CLEAN_FAILURE, SLOT_RETAINED } phase_t;
typedef struct scope_state {
    esp_cpu_task_scope_t token;
    TaskHandle_t creator;
    bool closed, retained, admission_closing;
    unsigned operations;
} scope_state_t;
typedef struct {
    esp_cpu_scoped_task_t token;
    scope_state_t *scope;
    phase_t phase;
    TaskHandle_t identity, created_identity;
    esp_cpu_scoped_entry_t entry;
    void *argument, *proxy_data;
    char name[configMAX_TASK_NAME_LEN];
    bool proxy_uses_pthread, proxy_installed, proxy_entered, go, held_publication;
    unsigned resident_operations;
    esp_cpu_retirement_result_t setup_error;
} task_state_t;

static DRAM_ATTR portMUX_TYPE scope_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR scope_state_t scope_states[ESP_CPU_RETIREMENT_SCOPE_LIMIT];
static DRAM_ATTR task_state_t task_states[ESP_CPU_RETIREMENT_TICKET_LIMIT];
static DRAM_ATTR unsigned key_state; /* 0 fresh, 1 initializing, 2 ready */
static DRAM_ATTR pthread_key_t marker_key;

static bool task_context(void) {
    return !xPortInIsrContext() && xTaskGetCurrentTaskHandle() != NULL;
}
static scope_state_t *find_scope(uint64_t id) {
    for (unsigned i = 0; id && i < ESP_CPU_RETIREMENT_SCOPE_LIMIT; ++i)
        if (scope_states[i].token.id == id) return &scope_states[i];
    return NULL;
}
static task_state_t *find_task(esp_cpu_scoped_task_t token) {
    for (unsigned i = 0; token.scope && token.serial && i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i)
        if (task_states[i].token.scope == token.scope && task_states[i].token.serial == token.serial)
            return &task_states[i];
    return NULL;
}
static task_state_t *current_registered(void) {
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i)
        if (task_states[i].identity == current) return &task_states[i];
    return NULL;
}
static bool authorized(scope_state_t *scope, TaskHandle_t caller) {
    if (!scope || scope->closed || scope->retained) return false;
    if (scope->creator == caller) return true;
    for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
        const task_state_t *task = &task_states[i];
        if (task->scope == scope && task->identity == caller && task->phase == SLOT_RUNNING)
            return true;
    }
    return false;
}
static void latch_retained(task_state_t *task) {
    portENTER_CRITICAL(&scope_lock);
    task->scope->retained = true;
    task->phase = SLOT_RETAINED;
    portEXIT_CRITICAL(&scope_lock);
}
static _Noreturn void park(void) {
    /* An ISR cannot wait. This is a resident fail-stop, not fake void success. */
    if (!task_context()) abort();
    for (;;) vTaskDelay(1);
}

static esp_cpu_retirement_result_t initialize_key(void) {
    portENTER_CRITICAL(&scope_lock);
    if (key_state) {
        const esp_cpu_retirement_result_t r = key_state == 2 ? ESP_CPU_RETIREMENT_OK : ESP_CPU_RETIREMENT_PENDING;
        portEXIT_CRITICAL(&scope_lock);
        return r;
    }
    key_state = 1;
    portEXIT_CRITICAL(&scope_lock);
    pthread_key_t created;
    const int rc = pthread_key_create(&created, NULL);
    portENTER_CRITICAL(&scope_lock);
    if (!rc) { marker_key = created; key_state = 2; }
    else key_state = 0; /* exact pthread failure creates no key */
    portEXIT_CRITICAL(&scope_lock);
    return rc ? ESP_CPU_RETIREMENT_EXHAUSTED : ESP_CPU_RETIREMENT_OK;
}

/* Kernel-only resident callback. It runs before the newlib/TCB cleanup tail.
 * It deliberately does NOT acknowledge retirement and never reads freed TCBs.
 * Private destructors, if admitted, must use this same resident pthread registry.
 * Callback code must not install private cleanup on foreign/idle task contexts;
 * that closed-custody condition remains a future sealed-image admission duty. */
static void tls_proxy(int index, void *data) {
    task_state_t *task = NULL;
    if (index != 0 || !task_context()) return;
    portENTER_CRITICAL(&scope_lock);
    for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
        task_state_t *candidate = &task_states[i];
        if (candidate->proxy_installed && candidate->proxy_data == data &&
            candidate->identity && !candidate->scope->closed) {
            task = candidate;
            break;
        }
    }
    if (!task || task->proxy_entered) {
        if (task) { task->scope->retained = true; task->phase = SLOT_RETAINED; }
        portEXIT_CRITICAL(&scope_lock);
        return;
    }
    task->proxy_entered = true;
    const TaskHandle_t target = task->identity;
    const bool use_pthread = task->proxy_uses_pthread;
    portEXIT_CRITICAL(&scope_lock);
    if (esp_cpu_retirement_cleanup_enter(task->token, target) != ESP_CPU_RETIREMENT_OK) {
        latch_retained(task);
        return;
    }
    if (use_pthread) pthread_internal_local_storage_destructor_callback(target);
    else vTaskSetThreadLocalStoragePointerAndDelCallback(target, 0, NULL, NULL);
    portENTER_CRITICAL(&scope_lock);
    task->proxy_installed = false;
    task->proxy_data = NULL; /* list may now be reused; no later lookup uses it */
    portEXIT_CRITICAL(&scope_lock);
    if (esp_cpu_retirement_cleanup_returned(task->token) != ESP_CPU_RETIREMENT_OK)
        latch_retained(task);
    /* No completion flag here: the actual kernel still owns its later tail. */
}

static esp_cpu_retirement_result_t install_proxy(task_state_t *task) {
    int rc = pthread_setspecific(marker_key, task);
    void *list = pvTaskGetThreadLocalStoragePointer(NULL, 0);
    /* A just-freed pthread list can be reused on another core before its old
     * resident proxy has returned. Refuse that pre-entry setup, rather than
     * treating a reused callback-data address as the old task's authority. */
    bool collision = false;
    portENTER_CRITICAL(&scope_lock);
    for (unsigned i = 0; list && i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i)
        if (&task_states[i] != task && task_states[i].proxy_installed &&
            task_states[i].proxy_data == list) collision = true;
    if (!collision && list) {
        task->proxy_uses_pthread = true;
        task->proxy_data = list;
        task->proxy_installed = true;
    }
    portEXIT_CRITICAL(&scope_lock);
    if (collision) {
        /* This new child has run only resident setup; its list contains only
         * the resident no-destructor marker. Dispose it before using sentinel. */
        pthread_internal_local_storage_destructor_callback(NULL);
        list = NULL;
        rc = ENOMEM;
    }
    /* If allocation failed before the list existed, a resident sentinel is safe
     * only because this child will exit without ever entering private code.
     * A partial allocation's real list must still run its complete destructor. */
    void *data = list ? list : task;
    portENTER_CRITICAL(&scope_lock);
    task->proxy_uses_pthread = list != NULL;
    task->proxy_data = data;
    task->proxy_installed = true;
    portEXIT_CRITICAL(&scope_lock);
    vTaskSetThreadLocalStoragePointerAndDelCallback(NULL, 0, data, tls_proxy);
    if (pvTaskGetThreadLocalStoragePointer(NULL, 0) != data) return ESP_CPU_RETIREMENT_RETAINED;
    return rc ? ESP_CPU_RETIREMENT_EXHAUSTED : ESP_CPU_RETIREMENT_OK;
}

static _Noreturn void self_exit(task_state_t *task) {
    portENTER_CRITICAL(&scope_lock);
    const bool busy = task->resident_operations != 0;
    const bool other_delete = task->phase == SLOT_DIRECT_EXIT || task->phase == SLOT_DELETE_WAIT;
    if (busy) { task->scope->retained = true; task->phase = SLOT_RETAINED; }
    else if (!other_delete) task->phase = SLOT_SELF_EXIT;
    portEXIT_CRITICAL(&scope_lock);
    if (busy) park(); /* never delete a task with an admitted resident frame */
    if (other_delete) park(); /* admitted delete-other owns the one kernel call */
    vTaskDelete(NULL);
    latch_retained(task); /* actual self-delete must never return here */
    park();
}

static void trampoline(void *argument) {
    task_state_t *task = argument;
    TaskHandle_t identity = NULL;
    esp_cpu_retirement_result_t rc = esp_cpu_retirement_register_current(task->token, &identity);
    if (rc != ESP_CPU_RETIREMENT_OK) {
        /* No private entry, TLS callback or data has been installed. The rejected
         * resident child still self-deletes; the failed observer ticket retains
         * its scope. No reused raw identity is reassigned or published. */
        latch_retained(task);
        vTaskDelete(NULL);
        park();
    }
    portENTER_CRITICAL(&scope_lock);
    task->identity = identity;
    portEXIT_CRITICAL(&scope_lock);
    rc = install_proxy(task);
    portENTER_CRITICAL(&scope_lock);
    task->setup_error = rc;
    if (rc == ESP_CPU_RETIREMENT_OK) task->phase = SLOT_READY;
    else if (rc == ESP_CPU_RETIREMENT_RETAINED) task->scope->retained = true;
    portEXIT_CRITICAL(&scope_lock);
    if (rc != ESP_CPU_RETIREMENT_OK) self_exit(task);
    for (;;) {
        portENTER_CRITICAL(&scope_lock);
        const bool retained = task->scope->retained;
        const bool ready = task->go;
        const bool deleting = task->phase == SLOT_DIRECT_EXIT || task->phase == SLOT_DELETE_WAIT;
        if (ready && !retained && !deleting) task->phase = SLOT_RUNNING;
        portEXIT_CRITICAL(&scope_lock);
        if (deleting) park();
        if (retained) self_exit(task);
        if (ready) break;
        vTaskDelay(1);
    }
    task->entry(task->argument);
    self_exit(task); /* private normal return permanently transfers to resident */
}

esp_cpu_retirement_result_t esp_cpu_task_scope_open(uint32_t budget, esp_cpu_task_scope_t *out) {
    if (!out || !budget || budget > ESP_CPU_RETIREMENT_TICKET_LIMIT) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    const bool nested = current_registered() != NULL;
    portEXIT_CRITICAL(&scope_lock);
    if (nested) return ESP_CPU_RETIREMENT_CONTEXT; /* owner keeper is not a scoped child */
    esp_cpu_retirement_result_t rc = esp_cpu_retirement_install();
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    rc = initialize_key();
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    esp_cpu_task_scope_t token;
    rc = esp_cpu_retirement_scope_open(budget, &token);
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *slot = NULL;
    for (unsigned i = 0; i < ESP_CPU_RETIREMENT_SCOPE_LIMIT; ++i)
        if (!scope_states[i].token.id) { slot = &scope_states[i]; break; }
    if (slot) {
        slot->token = token;
        slot->creator = xTaskGetCurrentTaskHandle();
        *out = token;
    }
    portEXIT_CRITICAL(&scope_lock);
    if (!slot) {
        rc = esp_cpu_retirement_scope_close(token);
        return rc == ESP_CPU_RETIREMENT_OK ? ESP_CPU_RETIREMENT_EXHAUSTED : ESP_CPU_RETIREMENT_RETAINED;
    }
    return ESP_CPU_RETIREMENT_OK;
}

esp_cpu_retirement_result_t esp_cpu_task_scope_close(esp_cpu_task_scope_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    esp_cpu_retirement_result_t rc = !scope || scope->closed ? ESP_CPU_RETIREMENT_STALE :
        scope->creator != xTaskGetCurrentTaskHandle() ? ESP_CPU_RETIREMENT_CONTEXT :
        scope->retained ? ESP_CPU_RETIREMENT_RETAINED : scope->operations ? ESP_CPU_RETIREMENT_PENDING : ESP_CPU_RETIREMENT_OK;
    if (rc == ESP_CPU_RETIREMENT_OK) ++scope->operations;
    portEXIT_CRITICAL(&scope_lock);
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    rc = esp_cpu_retirement_scope_close(token);
    portENTER_CRITICAL(&scope_lock);
    --scope->operations;
    if (rc == ESP_CPU_RETIREMENT_OK) {
        scope->closed = true;
        scope->creator = NULL;
        for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) if (task_states[i].scope == scope) {
            task_states[i].entry = NULL;
            task_states[i].argument = NULL;
            task_states[i].proxy_data = NULL;
            task_states[i].proxy_installed = false;
        }
    }
    portEXIT_CRITICAL(&scope_lock);
    return rc;
}

static esp_cpu_retirement_result_t create_internal_v1(esp_cpu_task_scope_t token,
    const esp_cpu_scoped_task_config_t *config, esp_cpu_scoped_task_publication_t *out, bool held) {
    if (!config || !out || !config->entry || !config->name || !config->stack_bytes ||
        config->priority >= configMAX_PRIORITIES ||
        (config->core != tskNO_AFFINITY && (config->core < 0 || config->core >= CONFIG_FREERTOS_NUMBER_OF_CORES)))
        return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    esp_cpu_task_stack_layout_v1_t layout;
    uint32_t stack_floor;
    if (!esp_cpu_task_stack_layout_current_v1(&layout) ||
        !esp_cpu_task_stack_floor_v1(&layout, config->working_stack_bytes, &stack_floor) ||
        config->stack_bytes < stack_floor) return ESP_CPU_RETIREMENT_INVALID;
    char name[configMAX_TASK_NAME_LEN];
    size_t length = 0;
    while (length < sizeof(name) && config->name[length]) { name[length] = config->name[length]; ++length; }
    if (!length || length == sizeof(name)) return ESP_CPU_RETIREMENT_INVALID;
    name[length] = 0;
    /* Copy all semantic inputs before the child could run. Caller pointers other
     * than provider-owned argument/entry are never retained. */
    const esp_cpu_scoped_task_config_t copy = *config;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    task_state_t *caller_task = current_registered();
    esp_cpu_retirement_result_t rc = !scope || scope->closed ? ESP_CPU_RETIREMENT_STALE :
        scope->retained ? ESP_CPU_RETIREMENT_RETAINED :
        scope->admission_closing ? ESP_CPU_RETIREMENT_INVALID :
        !authorized(scope, xTaskGetCurrentTaskHandle()) ? ESP_CPU_RETIREMENT_CONTEXT : ESP_CPU_RETIREMENT_OK;
    if (rc == ESP_CPU_RETIREMENT_OK) {
        ++scope->operations;
        if (caller_task) ++caller_task->resident_operations;
    }
    portEXIT_CRITICAL(&scope_lock);
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    esp_cpu_scoped_task_t ticket;
    rc = esp_cpu_retirement_reserve(token, &ticket);
    task_state_t *task = NULL;
    if (rc == ESP_CPU_RETIREMENT_OK) {
        portENTER_CRITICAL(&scope_lock);
        for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i)
            if (!task_states[i].token.serial) { task = &task_states[i]; break; }
        if (task) {
            task->token = ticket; task->scope = scope; task->phase = SLOT_CREATING;
            task->entry = copy.entry; task->argument = copy.argument;
            memcpy(task->name, name, length + 1);
        }
        portEXIT_CRITICAL(&scope_lock);
        if (!task) {
            rc = esp_cpu_retirement_abandon(ticket) == ESP_CPU_RETIREMENT_OK ?
                ESP_CPU_RETIREMENT_EXHAUSTED : ESP_CPU_RETIREMENT_RETAINED;
        }
    }
    if (task) {
        TaskHandle_t created = NULL;
        const BaseType_t made = xTaskCreatePinnedToCore(trampoline, task->name, copy.stack_bytes,
            task, copy.priority, &created, copy.core);
        if (made != pdPASS) {
            rc = esp_cpu_retirement_abandon(ticket) == ESP_CPU_RETIREMENT_OK ?
                ESP_CPU_RETIREMENT_EXHAUSTED : ESP_CPU_RETIREMENT_RETAINED;
            portENTER_CRITICAL(&scope_lock);
            task->phase = SLOT_CLEAN_FAILURE;
            portEXIT_CRITICAL(&scope_lock);
        } else {
            portENTER_CRITICAL(&scope_lock);
            task->created_identity = created;
            if (!created) scope->retained = true;
            portEXIT_CRITICAL(&scope_lock);
            for (;;) {
                portENTER_CRITICAL(&scope_lock);
                const bool retained = scope->retained;
                const phase_t phase = task->phase;
                const esp_cpu_retirement_result_t setup_error = task->setup_error;
                const bool mismatch = task->identity && task->identity != created;
                if (mismatch) scope->retained = true;
                if (!retained && !mismatch && phase == SLOT_READY) {
                    *out = (esp_cpu_scoped_task_publication_t){ticket, created};
                    task->held_publication = held;
                    task->go = !held; /* held mode waits for converted output publication */
                }
                portEXIT_CRITICAL(&scope_lock);
                if (retained || mismatch) { rc = ESP_CPU_RETIREMENT_RETAINED; break; }
                if (phase == SLOT_READY) { rc = ESP_CPU_RETIREMENT_OK; break; }
                if (setup_error != ESP_CPU_RETIREMENT_OK) {
                    esp_cpu_retirement_report_t report;
                    const esp_cpu_retirement_result_t observed = esp_cpu_retirement_poll(ticket, &report);
                    if (observed == ESP_CPU_RETIREMENT_OK) { rc = setup_error; break; }
                    if (observed != ESP_CPU_RETIREMENT_PENDING) { rc = ESP_CPU_RETIREMENT_RETAINED; break; }
                }
                vTaskDelay(1);
            }
        }
    }
    portENTER_CRITICAL(&scope_lock);
    --scope->operations;
    if (caller_task) --caller_task->resident_operations;
    if (rc == ESP_CPU_RETIREMENT_RETAINED) scope->retained = true;
    portEXIT_CRITICAL(&scope_lock);
    return rc;
}

esp_cpu_retirement_result_t esp_cpu_task_scope_create(esp_cpu_task_scope_t token,
    const esp_cpu_scoped_task_config_t *config, esp_cpu_scoped_task_publication_t *out) {
    return create_internal_v1(token, config, out, false); /* existing semantics */
}

esp_cpu_retirement_result_t esp_cpu_task_scope_poll(esp_cpu_scoped_task_t token,
    esp_cpu_retirement_report_t *out) {
    if (!out) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    task_state_t *task = find_task(token);
    task_state_t *caller = current_registered();
    if (caller && caller->phase != SLOT_RUNNING) {
        portEXIT_CRITICAL(&scope_lock);
        return ESP_CPU_RETIREMENT_CONTEXT;
    }
    if (caller) ++caller->resident_operations;
    const bool retained = task && task->scope->retained;
    portEXIT_CRITICAL(&scope_lock);
    const esp_cpu_retirement_result_t rc = task ? esp_cpu_retirement_poll(token, out) : ESP_CPU_RETIREMENT_STALE;
    portENTER_CRITICAL(&scope_lock);
    if (caller) --caller->resident_operations;
    portEXIT_CRITICAL(&scope_lock);
    if (retained && rc != ESP_CPU_RETIREMENT_STALE) { out->retained = true; return ESP_CPU_RETIREMENT_RETAINED; }
    return rc;
}

_Noreturn void esp_cpu_task_scope_exit_current(esp_cpu_task_scope_t token) {
    if (!task_context()) park();
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    task_state_t *task = NULL;
    for (unsigned i = 0; scope && !scope->closed && i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i)
        if (task_states[i].scope == scope && task_states[i].identity == xTaskGetCurrentTaskHandle()) {
            task = &task_states[i]; break;
        }
    portEXIT_CRITICAL(&scope_lock);
    if (!task) park();
    self_exit(task);
}

esp_cpu_retirement_result_t esp_cpu_task_scope_delete_join(esp_cpu_scoped_task_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    task_state_t *task = find_task(token);
    scope_state_t *scope = task ? task->scope : NULL;
    task_state_t *caller_task = current_registered();
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    esp_cpu_retirement_result_t rc = !task || scope->closed ? ESP_CPU_RETIREMENT_STALE :
        scope->retained ? ESP_CPU_RETIREMENT_RETAINED :
        !authorized(scope, current) ? ESP_CPU_RETIREMENT_CONTEXT : ESP_CPU_RETIREMENT_OK;
    const bool self = task && task->identity == current;
    const bool direct = rc == ESP_CPU_RETIREMENT_OK &&
        (task->phase == SLOT_RUNNING || (task->phase == SLOT_READY &&
         (task->go || task->held_publication)));
    if (rc == ESP_CPU_RETIREMENT_OK && !self) {
        if (!direct && task->phase != SLOT_DIRECT_EXIT && task->phase != SLOT_DELETE_WAIT &&
            task->phase != SLOT_SELF_EXIT) rc = ESP_CPU_RETIREMENT_STALE;
        else {
            ++scope->operations;
            if (caller_task) ++caller_task->resident_operations;
            if (direct) task->phase = SLOT_DELETE_WAIT;
        }
    }
    portEXIT_CRITICAL(&scope_lock);
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    if (self) self_exit(task);
    if (direct) {
        /* The one claiming deleter waits for all already-admitted resident
         * operations. DELETE_WAIT refuses fresh operations and makes target
         * self-exit park, so no registry frame is forcibly destroyed. */
        for (;;) {
            portENTER_CRITICAL(&scope_lock);
            const bool retained = scope->retained;
            const bool drained = task->resident_operations == 0;
            if (drained && !retained) task->phase = SLOT_DIRECT_EXIT;
            portEXIT_CRITICAL(&scope_lock);
            if (retained) park();
            if (drained) break;
            vTaskDelay(1);
        }
        rc = esp_cpu_retirement_direct_begin(token);
        if (rc != ESP_CPU_RETIREMENT_OK) { latch_retained(task); park(); }
        vTaskDelete(task->identity); /* exact registered task, one race-gated call */
        rc = esp_cpu_retirement_direct_returned(token);
        if (rc != ESP_CPU_RETIREMENT_OK && rc != ESP_CPU_RETIREMENT_PENDING) { latch_retained(task); park(); }
    }
    for (;;) {
        esp_cpu_retirement_report_t report;
        /* Already-admitted call: do not re-enter public operation admission if
         * this caller itself has since received a DELETE_WAIT request. */
        portENTER_CRITICAL(&scope_lock);
        const bool retained = scope->retained;
        portEXIT_CRITICAL(&scope_lock);
        rc = retained ? ESP_CPU_RETIREMENT_RETAINED : esp_cpu_retirement_poll(token, &report);
        if (rc == ESP_CPU_RETIREMENT_OK) break;
        if (rc != ESP_CPU_RETIREMENT_PENDING) { latch_retained(task); park(); }
        vTaskDelay(1);
    }
    portENTER_CRITICAL(&scope_lock);
    --scope->operations;
    if (caller_task) --caller_task->resident_operations;
    portEXIT_CRITICAL(&scope_lock);
    return ESP_CPU_RETIREMENT_OK;
}

void esp_cpu_task_scope_delete_or_park(esp_cpu_scoped_task_t token) {
    if (esp_cpu_task_scope_delete_join(token) != ESP_CPU_RETIREMENT_OK) park();
}

#include "esp_cpu_task_scope_extensions_v1.inc"

esp_cpu_retirement_result_t esp_cpu_task_scope_current_v1(
    esp_cpu_task_scope_t token, esp_cpu_task_scope_current_v1_t *out) {
    if (!out || !token.id) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    esp_cpu_retirement_result_t rc = !scope || scope->closed ? ESP_CPU_RETIREMENT_STALE :
        scope->retained ? ESP_CPU_RETIREMENT_RETAINED :
        !authorized(scope, caller) ? ESP_CPU_RETIREMENT_CONTEXT : ESP_CPU_RETIREMENT_OK;
    if (rc == ESP_CPU_RETIREMENT_OK) {
        esp_cpu_task_scope_current_v1_t value = {0};
        value.identity = caller;
        value.creator = scope->creator == caller;
        if (!value.creator) {
            task_state_t *task = current_registered();
            if (!task || task->scope != scope || task->phase != SLOT_RUNNING)
                rc = ESP_CPU_RETIREMENT_CONTEXT;
            else value.ticket = task->token;
        }
        if (rc == ESP_CPU_RETIREMENT_OK) *out = value;
    }
    portEXIT_CRITICAL(&scope_lock);
    return rc;
}

esp_cpu_retirement_result_t esp_cpu_task_scope_resolve_v1(
    esp_cpu_task_scope_t token, TaskHandle_t identity, esp_cpu_scoped_task_t *out) {
    if (!out || !token.id || !identity) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    esp_cpu_retirement_result_t rc = !scope || scope->closed ? ESP_CPU_RETIREMENT_STALE :
        scope->retained ? ESP_CPU_RETIREMENT_RETAINED :
        !authorized(scope, caller) ? ESP_CPU_RETIREMENT_CONTEXT : ESP_CPU_RETIREMENT_OK;
    if (rc == ESP_CPU_RETIREMENT_OK) {
        task_state_t *match = NULL;
        unsigned matches = 0;
        for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
            task_state_t *task = &task_states[i];
            if (task->scope == scope && task->identity == identity) {
                match = task;
                ++matches;
            }
        }
        if (identity == scope->creator || matches == 0) rc = ESP_CPU_RETIREMENT_STALE;
        else if (matches != 1 || !match->token.serial ||
                 match->token.scope != scope->token.id || match->phase == SLOT_RETAINED)
            rc = ESP_CPU_RETIREMENT_RETAINED;
        else *out = match->token;
    }
    portEXIT_CRITICAL(&scope_lock);
    return rc;
}


