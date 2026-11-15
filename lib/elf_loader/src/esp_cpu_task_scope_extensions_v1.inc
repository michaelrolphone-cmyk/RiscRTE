/* Included only by the existing private scope implementation. Reuses its
 * internal-DRAM lock, registry, operation counters and exact ticket identity. */
esp_cpu_retirement_result_t esp_cpu_task_scope_frame_enter_v1(
    esp_cpu_task_scope_t token, esp_cpu_task_scope_frame_v1 *frame) {
    if (!frame || frame->active || !token.id) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    task_state_t *caller = current_registered();
    /* Establish real live membership BEFORE reporting retained state. A
     * retained unrelated attachment must not poison a caller's scope lookup. */
    const bool member = scope && (scope->creator == current ||
        (caller && caller->scope == scope &&
         (caller->phase == SLOT_RUNNING || caller->phase == SLOT_RETAINED)));
    esp_cpu_retirement_result_t rc = !scope || scope->closed ? ESP_CPU_RETIREMENT_STALE :
        !member ? ESP_CPU_RETIREMENT_CONTEXT :
        scope->retained ? ESP_CPU_RETIREMENT_RETAINED :
        !authorized(scope, current) ? ESP_CPU_RETIREMENT_CONTEXT :
        scope->operations == UINT_MAX || (caller && caller->resident_operations == UINT_MAX) ?
        ESP_CPU_RETIREMENT_EXHAUSTED : ESP_CPU_RETIREMENT_OK;
    if (rc == ESP_CPU_RETIREMENT_OK) {
        ++scope->operations;
        if (caller) ++caller->resident_operations;
        *frame = (esp_cpu_task_scope_frame_v1){token,
            caller ? caller->token : (esp_cpu_scoped_task_t){0}, current, true};
    }
    portEXIT_CRITICAL(&scope_lock);
    return rc;
}

static esp_cpu_retirement_result_t frame_finish_v1(esp_cpu_task_scope_frame_v1 *frame,
    void *out1, const void *in1, size_t size1, void *out2, const void *in2, size_t size2,
    esp_cpu_scoped_task_t release_child) {
    if (size1 > 64 || size2 > 64 || (size1 && (!out1 || !in1)) || (size2 && (!out2 || !in2)))
        return ESP_CPU_RETIREMENT_INVALID;
    if (!frame || !frame->active) return ESP_CPU_RETIREMENT_INVALID;
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(frame->scope.id);
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    task_state_t *caller = frame->caller.serial ? find_task(frame->caller) : NULL;
    esp_cpu_retirement_result_t rc = !scope || scope->closed || !scope->operations ?
        ESP_CPU_RETIREMENT_RETAINED : current != frame->identity ? ESP_CPU_RETIREMENT_CONTEXT :
        frame->caller.serial ?
            (!caller || caller->scope != scope || caller->identity != current ||
             !caller->resident_operations ? ESP_CPU_RETIREMENT_RETAINED : ESP_CPU_RETIREMENT_OK) :
            (scope->creator == current ? ESP_CPU_RETIREMENT_OK : ESP_CPU_RETIREMENT_CONTEXT);
    task_state_t *child = release_child.serial ? find_task(release_child) : NULL;
    if (rc == ESP_CPU_RETIREMENT_OK && release_child.serial) {
        if (scope->retained) rc = ESP_CPU_RETIREMENT_RETAINED;
        else if (!child || child->scope != scope || child == caller ||
                 child->phase != SLOT_READY || !child->held_publication || child->go ||
                 !child->identity || child->identity != child->created_identity)
            rc = ESP_CPU_RETIREMENT_INVALID;
    }
    if (rc == ESP_CPU_RETIREMENT_OK) {
        if (size1) memcpy(out1, in1, size1);
        if (size2) memcpy(out2, in2, size2);
        if (child) {
            child->held_publication = false;
            child->go = true; /* outputs precede private entry under scope_lock */
        }
        --scope->operations;
        if (caller) --caller->resident_operations;
        *frame = (esp_cpu_task_scope_frame_v1){0};
    }
    /* DELETE_WAIT/retained does not forbid leaving a previously entered frame. */
    portEXIT_CRITICAL(&scope_lock);
    return rc;
}

esp_cpu_retirement_result_t esp_cpu_task_scope_frame_publish_v1(esp_cpu_task_scope_frame_v1 *frame,
    void *out1, const void *in1, size_t size1, void *out2, const void *in2, size_t size2) {
    return frame_finish_v1(frame, out1, in1, size1, out2, in2, size2, (esp_cpu_scoped_task_t){0});
}

esp_cpu_retirement_result_t esp_cpu_task_scope_create_held_v1(esp_cpu_task_scope_t token,
    const esp_cpu_scoped_task_config_t *config, esp_cpu_scoped_task_publication_t *out) {
    return create_internal_v1(token, config, out, true);
}

esp_cpu_retirement_result_t esp_cpu_task_scope_frame_publish_child_v1(esp_cpu_task_scope_frame_v1 *frame,
    esp_cpu_scoped_task_t child, void *out1, const void *in1, size_t size1,
    void *out2, const void *in2, size_t size2) {
    if (!child.scope || !child.serial || !size1 || !size2) return ESP_CPU_RETIREMENT_INVALID;
    return frame_finish_v1(frame, out1, in1, size1, out2, in2, size2, child);
}

esp_cpu_retirement_result_t esp_cpu_task_scope_frame_leave_v1(esp_cpu_task_scope_frame_v1 *frame) {
    return esp_cpu_task_scope_frame_publish_v1(frame, NULL, NULL, 0, NULL, NULL, 0);
}

static bool scope_designated_owner(scope_state_t *scope, esp_cpu_scoped_task_t owner) {
    task_state_t *task = find_task(owner);
    return task && task->scope == scope && task->identity &&
        (scope->creator == xTaskGetCurrentTaskHandle() ||
         (task->identity == xTaskGetCurrentTaskHandle() && task->phase == SLOT_RUNNING));
}

esp_cpu_retirement_result_t esp_cpu_task_scope_seal_v1(
    esp_cpu_task_scope_t token, esp_cpu_scoped_task_t owner) {
    esp_cpu_task_scope_frame_v1 frame = {0};
    esp_cpu_retirement_result_t rc = esp_cpu_task_scope_frame_enter_v1(token, &frame);
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    if (!scope_designated_owner(scope, owner)) rc = ESP_CPU_RETIREMENT_CONTEXT;
    else scope->admission_closing = true;
    portEXIT_CRITICAL(&scope_lock);
    const esp_cpu_retirement_result_t left = esp_cpu_task_scope_frame_leave_v1(&frame);
    return left == ESP_CPU_RETIREMENT_OK ? rc : ESP_CPU_RETIREMENT_RETAINED;
}

esp_cpu_retirement_result_t esp_cpu_task_scope_peers_retired_v1(
    esp_cpu_task_scope_t token, esp_cpu_scoped_task_t owner) {
    esp_cpu_task_scope_frame_v1 frame = {0};
    esp_cpu_retirement_result_t rc = esp_cpu_task_scope_frame_enter_v1(token, &frame);
    if (rc != ESP_CPU_RETIREMENT_OK) return rc;
    esp_cpu_scoped_task_t peers[ESP_CPU_RETIREMENT_TICKET_LIMIT];
    unsigned count = 0;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    if (!scope_designated_owner(scope, owner)) rc = ESP_CPU_RETIREMENT_CONTEXT;
    else if (!scope->admission_closing) rc = ESP_CPU_RETIREMENT_INVALID;
    else if (scope->operations != 1) rc = ESP_CPU_RETIREMENT_PENDING;
    else for (unsigned i = 0; i < ESP_CPU_RETIREMENT_TICKET_LIMIT; ++i) {
        const task_state_t *task = &task_states[i];
        if (task->scope == scope && task->token.serial != owner.serial)
            peers[count++] = task->token;
    }
    portEXIT_CRITICAL(&scope_lock);
    for (unsigned i = 0; rc == ESP_CPU_RETIREMENT_OK && i < count; ++i) {
        esp_cpu_retirement_report_t report;
        rc = esp_cpu_retirement_poll(peers[i], &report);
    }
    const esp_cpu_retirement_result_t left = esp_cpu_task_scope_frame_leave_v1(&frame);
    return left == ESP_CPU_RETIREMENT_OK ? rc : ESP_CPU_RETIREMENT_RETAINED;
}

esp_cpu_retirement_result_t esp_cpu_task_scope_retain_v1(esp_cpu_task_scope_t token) {
    if (!task_context()) return ESP_CPU_RETIREMENT_CONTEXT;
    portENTER_CRITICAL(&scope_lock);
    scope_state_t *scope = find_scope(token.id);
    esp_cpu_retirement_result_t rc = !scope || scope->closed ? ESP_CPU_RETIREMENT_STALE :
        scope->retained ? ESP_CPU_RETIREMENT_RETAINED :
        !authorized(scope, xTaskGetCurrentTaskHandle()) ? ESP_CPU_RETIREMENT_CONTEXT : ESP_CPU_RETIREMENT_OK;
    if (rc == ESP_CPU_RETIREMENT_OK) scope->retained = true;
    portEXIT_CRITICAL(&scope_lock);
    return rc;
}
