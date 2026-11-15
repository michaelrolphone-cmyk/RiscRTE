/* Candidate resident attachment + actual scoped owner. Not exported/installed.
 * The Module attachment and its worker descriptor are ONE record. No image or
 * raw-task identity registry is duplicated. All task tails use the existing
 * private scope/observer; worker-v2 source/semantics remain unchanged. */
#include "private/esp_cpu_task_scope_binding_v1.h"
#include "private/esp_cpu_task_scope_binding_v2.h"
#include "private/esp_cpu_task_scope_extensions_v1.h"
#include "esp_attr.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if !defined(RISC_CPU_SCOPE_V5_CANDIDATE) || RISC_CPU_SCOPE_V5_CANDIDATE != 1
#error "Native task scope v5 is an unadmitted candidate"
#endif

/* This candidate forwards the private result vocabulary. Pin EVERY value,
 * including the deliberately differently named exhaustion/limit result. */
_Static_assert((int)ESP_CPU_RETIREMENT_OK == (int)RISC_CPU_SCOPE_OK, "scope OK mapping");
_Static_assert((int)ESP_CPU_RETIREMENT_PENDING == (int)RISC_CPU_SCOPE_PENDING, "scope pending mapping");
_Static_assert((int)ESP_CPU_RETIREMENT_INVALID == (int)RISC_CPU_SCOPE_INVALID, "scope invalid mapping");
_Static_assert((int)ESP_CPU_RETIREMENT_CONTEXT == (int)RISC_CPU_SCOPE_CONTEXT, "scope context mapping");
_Static_assert((int)ESP_CPU_RETIREMENT_EXHAUSTED == (int)RISC_CPU_SCOPE_LIMIT, "scope exhaustion mapping");
_Static_assert((int)ESP_CPU_RETIREMENT_STALE == (int)RISC_CPU_SCOPE_STALE, "scope stale mapping");
_Static_assert((int)ESP_CPU_RETIREMENT_RETAINED == (int)RISC_CPU_SCOPE_RETAINED, "scope retained mapping");

typedef enum { V5_EMPTY, V5_WINDOW, V5_STAGED, V5_COMMITTED,
               V5_CLOSING, V5_CANCEL, V5_CLOSED, V5_RETAINED } phase_v5;
typedef struct {
    uint64_t token;
    TaskHandle_t creator;
    esp_cpu_task_mapping_v1 mapping;
    esp_cpu_task_region_v1 internal_executable, internal_data, internal_bss;
    esp_cpu_task_scope_t scope;
    esp_cpu_scoped_task_publication_t owner;
    risc_cpu_task_config_v1 config;
    phase_v5 phase;
    bool creating, entered, returned, descriptor_released;
} slot_v5;
#define V5_SLOTS 4u
#define V5_STACK_MAX 32768u
static DRAM_ATTR slot_v5 slots_v5[V5_SLOTS];
static DRAM_ATTR uint64_t generation_v5;
static DRAM_ATTR uint64_t window_v5;
static DRAM_ATTR portMUX_TYPE lock_v5 = portMUX_INITIALIZER_UNLOCKED;

static bool task_context_v5(void) {
    return !xPortInIsrContext() && xTaskGetCurrentTaskHandle() != NULL;
}
static slot_v5 *find_v5(uint64_t token) {
    const unsigned index = (unsigned)(token & 255u);
    if (!index || index > V5_SLOTS) return NULL;
    return slots_v5[index - 1].token == token ? &slots_v5[index - 1] : NULL;
}
static bool region_valid(esp_cpu_task_region_v1 r) {
    return (!r.begin && !r.bytes) || (r.begin && r.bytes && r.bytes <= UINTPTR_MAX - r.begin);
}
static bool contains(esp_cpu_task_region_v1 r, uintptr_t value, size_t bytes) {
    return value >= r.begin && bytes && value - r.begin < r.bytes &&
        bytes <= r.bytes - (value - r.begin);
}
static bool config_valid(const slot_v5 *slot, const risc_cpu_task_config_v1 *c, bool owner) {
    if (!c || c->struct_size != sizeof(*c) || !c->entry || !c->stack_bytes ||
        c->stack_bytes > V5_STACK_MAX || !c->working_stack_bytes ||
        c->priority >= configMAX_PRIORITIES || (owner && (!c->priority || c->priority > 5)) ||
        c->core < -1 || c->core >= CONFIG_FREERTOS_NUMBER_OF_CORES ||
        !(contains(slot->mapping.executable, (uintptr_t)c->entry, 1) ||
          contains(slot->internal_executable, (uintptr_t)c->entry, 1))) return false;
    size_t length = 0;
    while (length < sizeof(c->name) && c->name[length]) ++length;
    if (!length || length == sizeof(c->name)) return false;
    if (!c->argument) return c->argument_bytes == 0;
    return c->argument_bytes &&
        (contains(slot->mapping.data, (uintptr_t)c->argument, c->argument_bytes) ||
         contains(slot->mapping.bss, (uintptr_t)c->argument, c->argument_bytes) ||
         contains(slot->internal_data, (uintptr_t)c->argument, c->argument_bytes) ||
         contains(slot->internal_bss, (uintptr_t)c->argument, c->argument_bytes));
}
static _Noreturn void park_v5(void) {
    if (!task_context_v5()) abort();
    for (;;) vTaskDelay(1);
}
static _Noreturn void retained_v5(slot_v5 *slot) {
    portENTER_CRITICAL(&lock_v5);
    slot->phase = V5_RETAINED;
    const esp_cpu_task_scope_t scope = slot->scope;
    portEXIT_CRITICAL(&lock_v5);
    (void)esp_cpu_task_scope_retain_v1(scope);
    park_v5();
}

static void owner_gate_v5(void *argument) {
    slot_v5 *slot = argument;
    for (;;) {
        portENTER_CRITICAL(&lock_v5);
        const phase_v5 phase = slot->phase;
        if (phase == V5_CANCEL) {
            slot->returned = true;
            portEXIT_CRITICAL(&lock_v5);
            return; /* private entry NEVER ran; actual007 owns the cleanup tail */
        }
        if (phase == V5_COMMITTED) {
            slot->entered = true;
            void (*entry)(void *) = slot->config.entry;
            void *parameter = slot->config.argument;
            portEXIT_CRITICAL(&lock_v5);
            entry(parameter);
            portENTER_CRITICAL(&lock_v5);
            const bool closing = slot->phase == V5_CLOSING;
            const esp_cpu_task_scope_t scope = slot->scope;
            const esp_cpu_scoped_task_t owner = slot->owner.ticket;
            portEXIT_CRITICAL(&lock_v5);
            if (!closing || esp_cpu_task_scope_peers_retired_v1(scope, owner) != ESP_CPU_RETIREMENT_OK)
                retained_v5(slot);
            portENTER_CRITICAL(&lock_v5);
            slot->returned = true;
            portEXIT_CRITICAL(&lock_v5);
            return; /* NOT completion: TLS/newlib/TCB and idle ACK still follow */
        }
        portEXIT_CRITICAL(&lock_v5);
        if (phase == V5_RETAINED || phase == V5_CLOSED || phase == V5_EMPTY)
            retained_v5(slot);
        vTaskDelay(1); /* only resident code while Graph grant is unpublished */
    }
}

static int binding_begin_impl(const esp_cpu_task_mapping_v1 *map,
                              const esp_cpu_task_mapping_v2 *extra,
                              esp_cpu_task_binding_v1 *output) {
    if (!map || !output || output->token || !map->module || !map->mapping ||
        !map->executable.begin || !map->executable.bytes ||
        !region_valid(map->executable) || !region_valid(map->data) || !region_valid(map->bss))
        return RISC_CPU_SCOPE_INVALID;
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = NULL;
    if (!window_v5 && generation_v5 < (UINT64_MAX >> 8)) {
        for (unsigned i = 0; i < V5_SLOTS; ++i) if (!slots_v5[i].token) {
            slot = &slots_v5[i];
            slot->token = (++generation_v5 << 8) | (i + 1);
            slot->creator = xTaskGetCurrentTaskHandle();
            slot->mapping = *map;
            if (extra) {
                slot->internal_executable = extra->internal_executable;
                slot->internal_data = extra->internal_data;
                slot->internal_bss = extra->internal_bss;
            }
            slot->phase = V5_WINDOW;
            window_v5 = slot->token;
            break;
        }
    }
    portEXIT_CRITICAL(&lock_v5);
    if (!slot) return RISC_CPU_SCOPE_LIMIT;
    esp_cpu_task_scope_t created = {0};
    const int rc = esp_cpu_task_scope_open(ESP_CPU_RETIREMENT_TICKET_LIMIT, &created);
    portENTER_CRITICAL(&lock_v5);
    if (rc == ESP_CPU_RETIREMENT_OK) {
        slot->scope = created;
        output->token = slot->token;
    } else {
        window_v5 = 0;
        if (rc == ESP_CPU_RETIREMENT_RETAINED) {
            slot->phase = V5_RETAINED;
            output->token = slot->token; /* retained attachment must stay visible */
        } else *slot = (slot_v5){0};
    }
    portEXIT_CRITICAL(&lock_v5);
    return rc;
}

int esp_cpu_task_binding_begin_v1(const esp_cpu_task_mapping_v1 *map,
                                 esp_cpu_task_binding_v1 *output) {
    return binding_begin_impl(map, NULL, output);
}
int esp_cpu_task_binding_begin_v2(const esp_cpu_task_mapping_v2 *map,
                                 esp_cpu_task_binding_v1 *output) {
    /* Read the complete size/tag before any additive field. The host owns this
     * pointer; no provider path can nominate arbitrary Module/address spans. */
    if (!map || map->struct_size != sizeof(*map) || map->tag != ESP_CPU_TASK_MAPPING_TAG_V2)
        return RISC_CPU_SCOPE_INVALID;
    const esp_cpu_task_mapping_v2 copied = *map;
    const esp_cpu_task_region_v1 ranges[] = {copied.legacy.executable, copied.legacy.data,
        copied.legacy.bss, copied.internal_executable, copied.internal_data, copied.internal_bss};
    for (unsigned i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        if (!region_valid(ranges[i])) return RISC_CPU_SCOPE_INVALID;
        if (!ranges[i].bytes) continue;
        for (unsigned j = 0; j < i; ++j) {
            if (ranges[j].bytes && ranges[i].begin < ranges[j].begin + ranges[j].bytes &&
                ranges[j].begin < ranges[i].begin + ranges[i].bytes) return RISC_CPU_SCOPE_INVALID;
        }
    }
    return binding_begin_impl(&copied.legacy, &copied, output);
}

int risc_cpu_worker_start_v5(const risc_cpu_task_config_v1 *config, risc_cpu_worker_v5 *output) {
    if (!config || !output || *output) return RISC_CPU_SCOPE_INVALID;
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    const risc_cpu_task_config_v1 copied = *config;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(window_v5);
    int rc = !slot || slot->creator != xTaskGetCurrentTaskHandle() ? RISC_CPU_SCOPE_CONTEXT :
        slot->phase != V5_WINDOW || slot->creating || slot->owner.ticket.serial ? RISC_CPU_SCOPE_INVALID :
        !config_valid(slot, &copied, true) ? RISC_CPU_SCOPE_INVALID : RISC_CPU_SCOPE_OK;
    if (rc == RISC_CPU_SCOPE_OK) { slot->creating = true; slot->config = copied; }
    portEXIT_CRITICAL(&lock_v5);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    const esp_cpu_scoped_task_config_t task = {owner_gate_v5, slot, copied.name,
        copied.stack_bytes, copied.working_stack_bytes, copied.priority,
        copied.core < 0 ? tskNO_AFFINITY : copied.core};
    esp_cpu_scoped_task_publication_t made = {0};
    rc = esp_cpu_task_scope_create(slot->scope, &task, &made);
    portENTER_CRITICAL(&lock_v5);
    slot->creating = false;
    if (rc == ESP_CPU_RETIREMENT_OK) {
        slot->owner = made;
        slot->phase = V5_STAGED;
        *output = slot->token;
    } else if (rc == ESP_CPU_RETIREMENT_RETAINED) slot->phase = V5_RETAINED;
    portEXIT_CRITICAL(&lock_v5);
    return rc;
}

int esp_cpu_task_binding_end_v1(esp_cpu_task_binding_v1 binding, bool succeeded) {
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(binding.token);
    bool missing_owner = false;
    int rc = !slot ? RISC_CPU_SCOPE_STALE : slot->creator != xTaskGetCurrentTaskHandle() ||
        window_v5 != binding.token ? RISC_CPU_SCOPE_CONTEXT :
        slot->creating ? RISC_CPU_SCOPE_RETAINED : RISC_CPU_SCOPE_OK;
    if (rc == RISC_CPU_SCOPE_OK) {
        window_v5 = 0;
        if (slot->phase == V5_RETAINED) rc = RISC_CPU_SCOPE_RETAINED;
        else if (succeeded && slot->phase == V5_STAGED && slot->owner.ticket.serial) {
            /* Keep private entry closed until exact grant commit. */
        } else { missing_owner = succeeded; slot->phase = V5_CANCEL; rc = RISC_CPU_SCOPE_PENDING; }
    }
    portEXIT_CRITICAL(&lock_v5);
    if (rc == RISC_CPU_SCOPE_PENDING) {
        rc = risc_cpu_worker_join_v5(binding.token);
        return missing_owner && rc == RISC_CPU_SCOPE_OK ? RISC_CPU_SCOPE_INVALID : rc;
    }
    return rc;
}

int esp_cpu_task_binding_cancel_staged_v1(esp_cpu_task_binding_v1 binding) {
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(binding.token);
    int rc = !slot ? RISC_CPU_SCOPE_STALE :
        slot->creator != xTaskGetCurrentTaskHandle() ? RISC_CPU_SCOPE_CONTEXT :
        slot->phase == V5_RETAINED ? RISC_CPU_SCOPE_RETAINED :
        slot->phase == V5_CLOSED ? RISC_CPU_SCOPE_OK :
        window_v5 == binding.token || slot->creating ? RISC_CPU_SCOPE_CONTEXT :
        slot->phase == V5_CANCEL ? RISC_CPU_SCOPE_PENDING :
        slot->phase == V5_STAGED && !slot->entered ? RISC_CPU_SCOPE_PENDING : RISC_CPU_SCOPE_CONTEXT;
    if (rc == RISC_CPU_SCOPE_PENDING) slot->phase = V5_CANCEL;
    portEXIT_CRITICAL(&lock_v5);
    return rc == RISC_CPU_SCOPE_PENDING ? risc_cpu_worker_join_v5(binding.token) : rc;
}

bool esp_cpu_task_binding_commit_v1(esp_cpu_task_binding_v1 binding,
                                   const void *module, uint64_t mapping) {
    if (!task_context_v5()) return false;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(binding.token);
    const bool valid = slot && slot->creator == xTaskGetCurrentTaskHandle() &&
        window_v5 != binding.token && slot->mapping.module == module && mapping &&
        slot->mapping.mapping == mapping && slot->owner.ticket.serial &&
        (slot->phase == V5_STAGED || slot->phase == V5_COMMITTED);
    if (valid) slot->phase = V5_COMMITTED;
    portEXIT_CRITICAL(&lock_v5);
    return valid;
}

void esp_cpu_task_binding_retain_v1(esp_cpu_task_binding_v1 binding) {
    if (!task_context_v5()) return;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(binding.token);
    const bool permitted = slot && slot->creator == xTaskGetCurrentTaskHandle();
    esp_cpu_task_scope_t scope = {0};
    if (permitted) { slot->phase = V5_RETAINED; scope = slot->scope; }
    portEXIT_CRITICAL(&lock_v5);
    if (permitted && scope.id) (void)esp_cpu_task_scope_retain_v1(scope);
}

int risc_cpu_worker_join_v5(risc_cpu_worker_v5 token) {
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(token);
    int rc = !slot || slot->descriptor_released ? RISC_CPU_SCOPE_STALE : slot->creator != xTaskGetCurrentTaskHandle() ? RISC_CPU_SCOPE_CONTEXT :
        slot->phase == V5_RETAINED ? RISC_CPU_SCOPE_RETAINED :
        slot->phase == V5_CLOSED ? RISC_CPU_SCOPE_OK :
        window_v5 == token || slot->creating || (slot->phase != V5_CANCEL &&
         (slot->phase != V5_CLOSING || !slot->returned)) ? RISC_CPU_SCOPE_PENDING : RISC_CPU_SCOPE_OK;
    const bool closed = slot && slot->phase == V5_CLOSED;
    const esp_cpu_task_scope_t scope = slot ? slot->scope : (esp_cpu_task_scope_t){0};
    const esp_cpu_scoped_task_t owner = slot ? slot->owner.ticket : (esp_cpu_scoped_task_t){0};
    portEXIT_CRITICAL(&lock_v5);
    if (rc != RISC_CPU_SCOPE_OK || closed) return rc;
    if (owner.serial) {
        esp_cpu_retirement_report_t report;
        rc = esp_cpu_task_scope_poll(owner, &report);
        if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    }
    rc = esp_cpu_task_scope_close(scope); /* actual creator, all tickets + operations */
    if (rc == ESP_CPU_RETIREMENT_OK) {
        portENTER_CRITICAL(&lock_v5);
        slot->phase = V5_CLOSED;
        slot->config.entry = NULL;
        slot->config.argument = NULL;
        portEXIT_CRITICAL(&lock_v5);
    }
    return rc;
}

bool esp_cpu_task_binding_safe_v1(esp_cpu_task_binding_v1 binding) {
    if (!binding.token) return true;
    if (!task_context_v5()) return false;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(binding.token);
    const bool closed = slot && slot->creator == xTaskGetCurrentTaskHandle() && slot->phase == V5_CLOSED;
    portEXIT_CRITICAL(&lock_v5);
    return closed || risc_cpu_worker_join_v5(binding.token) == RISC_CPU_SCOPE_OK;
}
int risc_cpu_worker_release_v5(risc_cpu_worker_v5 token) {
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(token);
    const int rc = !slot || slot->descriptor_released ? RISC_CPU_SCOPE_STALE : slot->creator != xTaskGetCurrentTaskHandle() ?
        RISC_CPU_SCOPE_CONTEXT : slot->phase != V5_CLOSED ? RISC_CPU_SCOPE_PENDING : RISC_CPU_SCOPE_OK;
    if (rc == RISC_CPU_SCOPE_OK) slot->descriptor_released = true;
    portEXIT_CRITICAL(&lock_v5);
    return rc;
}
bool esp_cpu_task_binding_release_v1(esp_cpu_task_binding_v1 *binding) {
    if (!binding) return false;
    if (!binding->token) return true;
    if (!task_context_v5()) return false;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(binding->token);
    const bool safe = slot && slot->creator == xTaskGetCurrentTaskHandle() && slot->phase == V5_CLOSED;
    if (safe) *slot = (slot_v5){0};
    portEXIT_CRITICAL(&lock_v5);
    if (!safe) return false;
    binding->token = 0;
    return true;
}

/* Facade admission. A frame pins the existing scope operation, not merely an
 * identity snapshot. Slot cannot be released while its scope frame is entered. */
static int enter_api_v5(uint64_t token, slot_v5 **output,
                       esp_cpu_task_scope_frame_v1 *frame,
                       esp_cpu_task_scope_current_v1_t *current) {
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    portENTER_CRITICAL(&lock_v5);
    slot_v5 *slot = find_v5(token);
    const esp_cpu_task_scope_t scope = slot ? slot->scope : (esp_cpu_task_scope_t){0};
    portEXIT_CRITICAL(&lock_v5);
    if (!slot) return RISC_CPU_SCOPE_STALE;
    int rc = esp_cpu_task_scope_frame_enter_v1(scope, frame);
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    rc = esp_cpu_task_scope_current_v1(scope, current);
    portENTER_CRITICAL(&lock_v5);
    if (rc == ESP_CPU_RETIREMENT_OK) {
        if (find_v5(token) != slot || current->creator) rc = RISC_CPU_SCOPE_CONTEXT;
        else if (slot->phase == V5_RETAINED) rc = RISC_CPU_SCOPE_RETAINED;
        else if (slot->phase != V5_COMMITTED && slot->phase != V5_CLOSING) rc = RISC_CPU_SCOPE_CONTEXT;
    }
    portEXIT_CRITICAL(&lock_v5);
    if (rc != RISC_CPU_SCOPE_OK) {
        if (esp_cpu_task_scope_frame_leave_v1(frame) != ESP_CPU_RETIREMENT_OK) return RISC_CPU_SCOPE_RETAINED;
        return rc;
    }
    *output = slot;
    return RISC_CPU_SCOPE_OK;
}
static int leave_api_v5(esp_cpu_task_scope_frame_v1 *frame, int result) {
    return esp_cpu_task_scope_frame_leave_v1(frame) == ESP_CPU_RETIREMENT_OK ? result : RISC_CPU_SCOPE_RETAINED;
}

int risc_cpu_task_scope_current_v1(risc_cpu_task_scope_v1 *output) {
    if (!output) return RISC_CPU_SCOPE_INVALID;
    if (!task_context_v5()) return RISC_CPU_SCOPE_CONTEXT;
    /* At most four Module attachments; actual scope auth decides membership.
     * No raw current-task→mapping lookup table is introduced. */
    uint64_t tokens[V5_SLOTS];
    portENTER_CRITICAL(&lock_v5);
    for (unsigned i = 0; i < V5_SLOTS; ++i) tokens[i] = slots_v5[i].token;
    portEXIT_CRITICAL(&lock_v5);
    for (unsigned i = 0; i < V5_SLOTS; ++i) if (tokens[i]) {
        esp_cpu_task_scope_frame_v1 frame = {0};
        esp_cpu_task_scope_current_v1_t current;
        slot_v5 *slot;
        const int rc = enter_api_v5(tokens[i], &slot, &frame, &current);
        if (rc == RISC_CPU_SCOPE_OK) {
            return esp_cpu_task_scope_frame_publish_v1(&frame, output, &tokens[i], sizeof(tokens[i]), NULL, NULL, 0);
        }
        /* Skip only explicit nonmembership/stale attachment observations.
         * Frame-enter checked membership before returning RETAINED. */
        if (rc != RISC_CPU_SCOPE_CONTEXT && rc != RISC_CPU_SCOPE_STALE)
            return rc == RISC_CPU_SCOPE_RETAINED ? rc : RISC_CPU_SCOPE_RETAINED;
    }
    return RISC_CPU_SCOPE_CONTEXT;
}
int risc_cpu_task_current_v1(risc_cpu_task_scope_v1 token, risc_cpu_task_identity_v1 *output) {
    if (!output) return RISC_CPU_SCOPE_INVALID;
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    int rc = enter_api_v5(token, &slot, &frame, &current);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    const risc_cpu_task_identity_v1 value = {{current.ticket.scope, current.ticket.serial},
        (uintptr_t)current.identity, current.ticket.serial == slot->owner.ticket.serial};
    return esp_cpu_task_scope_frame_publish_v1(&frame, output, &value, sizeof(value), NULL, NULL, 0);
}
int risc_cpu_task_kernel_v1_get(risc_cpu_task_scope_v1 token, risc_cpu_task_kernel_v1 *output) {
    if (!output) return RISC_CPU_SCOPE_INVALID;
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    int rc = enter_api_v5(token, &slot, &frame, &current);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    (void)slot;
    const risc_cpu_task_kernel_v1 value = {portTICK_PERIOD_MS, configMAX_PRIORITIES, CONFIG_FREERTOS_NUMBER_OF_CORES};
    return esp_cpu_task_scope_frame_publish_v1(&frame, output, &value, sizeof(value), NULL, NULL, 0);
}
int risc_cpu_task_create_v1(risc_cpu_task_scope_v1 token, const risc_cpu_task_config_v1 *config,
                            risc_cpu_scoped_task_v1 *task_out, uintptr_t *identity_out) {
    if (!config || !task_out || !identity_out || task_out->scope || task_out->serial || *identity_out)
        return RISC_CPU_SCOPE_INVALID;
    const risc_cpu_task_config_v1 copy = *config;
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    int rc = enter_api_v5(token, &slot, &frame, &current);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    portENTER_CRITICAL(&lock_v5);
    const bool allowed = slot->phase == V5_COMMITTED && config_valid(slot, &copy, false);
    portEXIT_CRITICAL(&lock_v5);
    if (!allowed) return leave_api_v5(&frame, RISC_CPU_SCOPE_INVALID);
    esp_cpu_scoped_task_config_t task = {copy.entry, copy.argument, copy.name, copy.stack_bytes,
        copy.working_stack_bytes, copy.priority, copy.core < 0 ? tskNO_AFFINITY : copy.core};
    esp_cpu_scoped_task_publication_t publication = {0};
    rc = esp_cpu_task_scope_create_held_v1(slot->scope, &task, &publication);
    if (rc == ESP_CPU_RETIREMENT_OK) {
        const risc_cpu_scoped_task_v1 task_result = {publication.ticket.scope, publication.ticket.serial};
        const uintptr_t identity = (uintptr_t)publication.kernel_identity;
        rc = esp_cpu_task_scope_frame_publish_child_v1(&frame, publication.ticket,
            task_out, &task_result, sizeof(task_result), identity_out, &identity, sizeof(identity));
        if (rc != ESP_CPU_RETIREMENT_OK) retained_v5(slot); /* held custody, never clean refusal */
        return rc;
    }
    return leave_api_v5(&frame, rc);
}
int risc_cpu_task_resolve_v1(risc_cpu_task_scope_v1 token, uintptr_t identity, risc_cpu_scoped_task_v1 *output) {
    if (!output || !identity) return RISC_CPU_SCOPE_INVALID;
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    int rc = enter_api_v5(token, &slot, &frame, &current);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    esp_cpu_scoped_task_t task;
    rc = esp_cpu_task_scope_resolve_v1(slot->scope, (TaskHandle_t)identity, &task);
    if (rc == ESP_CPU_RETIREMENT_OK) {
        /* Logical owner must retire through its return/close protocol. */
        if (task.serial == slot->owner.ticket.serial) rc = RISC_CPU_SCOPE_CONTEXT;
    }
    if (rc == RISC_CPU_SCOPE_OK) {
        const risc_cpu_scoped_task_v1 value = {task.scope, task.serial};
        return esp_cpu_task_scope_frame_publish_v1(&frame, output, &value, sizeof(value), NULL, NULL, 0);
    }
    return leave_api_v5(&frame, rc);
}
int risc_cpu_task_delete_join_v1(risc_cpu_task_scope_v1 token, risc_cpu_scoped_task_v1 task) {
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    int rc = enter_api_v5(token, &slot, &frame, &current);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    if (task.scope != slot->scope.id || !task.serial || task.serial == slot->owner.ticket.serial)
        return leave_api_v5(&frame, RISC_CPU_SCOPE_CONTEXT);
    const esp_cpu_scoped_task_t exact = {task.scope, task.serial};
    if (current.ticket.serial == task.serial) {
        rc = leave_api_v5(&frame, RISC_CPU_SCOPE_OK);
        if (rc != RISC_CPU_SCOPE_OK) retained_v5(slot);
        esp_cpu_task_scope_exit_current(slot->scope);
    }
    rc = esp_cpu_task_scope_delete_join(exact);
    return leave_api_v5(&frame, rc);
}
void risc_cpu_task_exit_v1(risc_cpu_task_scope_v1 token) {
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    if (enter_api_v5(token, &slot, &frame, &current) != RISC_CPU_SCOPE_OK) park_v5();
    const bool owner = current.ticket.serial == slot->owner.ticket.serial;
    if (leave_api_v5(&frame, RISC_CPU_SCOPE_OK) != RISC_CPU_SCOPE_OK || owner) retained_v5(slot);
    esp_cpu_task_scope_exit_current(slot->scope);
}
void risc_cpu_task_retain_v1(risc_cpu_task_scope_v1 token) {
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    if (enter_api_v5(token, &slot, &frame, &current) != RISC_CPU_SCOPE_OK) park_v5();
    (void)leave_api_v5(&frame, RISC_CPU_SCOPE_OK);
    retained_v5(slot);
}
int risc_cpu_task_delay_v1(risc_cpu_task_scope_v1 token, uint32_t ticks) {
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    int rc = enter_api_v5(token, &slot, &frame, &current);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    (void)slot;
    vTaskDelay((TickType_t)ticks);
    return leave_api_v5(&frame, RISC_CPU_SCOPE_OK);
}
int risc_cpu_task_scope_request_close_v1(risc_cpu_task_scope_v1 token) {
    esp_cpu_task_scope_frame_v1 frame = {0}; esp_cpu_task_scope_current_v1_t current; slot_v5 *slot;
    int rc = enter_api_v5(token, &slot, &frame, &current);
    if (rc != RISC_CPU_SCOPE_OK) return rc;
    if (current.ticket.serial != slot->owner.ticket.serial)
        return leave_api_v5(&frame, RISC_CPU_SCOPE_CONTEXT);
    rc = esp_cpu_task_scope_seal_v1(slot->scope, slot->owner.ticket);
    if (rc == ESP_CPU_RETIREMENT_OK) {
        portENTER_CRITICAL(&lock_v5);
        if (slot->phase == V5_COMMITTED) slot->phase = V5_CLOSING;
        portEXIT_CRITICAL(&lock_v5);
    }
    /* Drop the facade frame before peer-drain query, which counts its own
     * entered frame. Creation is already fenced in the actual scope registry. */
    rc = leave_api_v5(&frame, rc);
    return rc == RISC_CPU_SCOPE_OK ? esp_cpu_task_scope_peers_retired_v1(slot->scope, slot->owner.ticket) : rc;
}
