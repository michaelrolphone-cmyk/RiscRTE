/* Scripted entry point around the unchanged BLE client.  The test executable
 * supplies hardware controls and links the production PortableApps adapter,
 * Runtime, scene provider and text-entry provider.  No capability is acquired
 * here merely to approximate an application's footprint.
 *
 * Modes: accepted (default), cancelled, back, navigation-back, pending-close,
 * retained-close, and handoff.  A retained close deliberately leaves custody
 * intact for Runtime's invocation fence; it must not be "cleaned up" by tests.
 */
#include "PortableApps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CAPACITY_BLE_SOURCE
#error "The fixture runner must select CAPACITY_BLE_SOURCE"
#endif
#define app_main original_ble_main
#include CAPACITY_BLE_SOURCE
#undef app_main

extern const char *capacity_mode(void);
extern const char *capacity_ble_expected(void);
extern bool capacity_hardware(void);
extern void capacity_phase(const char *phase);
extern void capacity_control(unsigned command);

enum {
    CAPACITY_KEY_A = 1,
    CAPACITY_KEY_ENTER = 2,
    CAPACITY_KEY_ESCAPE = 3,
    CAPACITY_DISPLAY_PENDING = 4,
    CAPACITY_DISPLAY_COMPLETE = 5,
    CAPACITY_NAVIGATION_BACK = 6,
    CAPACITY_FAIL_UNSUBSCRIBE = 9
};

static void capacity_ble_require(bool condition, const char *description) {
    if (condition) return;
    fprintf(stderr, "BLE capacity client: %s\n", description);
    abort();
}

static bool capacity_ble_mode(const char *mode, const char *value) {
    return mode && !strcmp(mode, value);
}

static void capacity_ble_closed(void) {
    capacity_ble_require(!naming && !name_closing && !name_client.active &&
                         !name_client.acquired && !name_client.suspended &&
                         !name_client.retained && !name_client.session &&
                         !name_client.grant.api && !name_client.grant.slot &&
                         !name_client.grant.generation && !uncertain,
                         "modal did not release its session and grant");
    capacity_ble_require(!portable_text_adapter_retained(),
                         "adapter retained after a successful close");
    capacity_ble_require(portable_app_before_launch(PORTABLE_RETURN_APP),
                         "app launch guard remained fenced after close");
}

/* Let the real host complete and expose its current frame before delivering a
 * scene-navigation event.  Otherwise its intentional neutral-input reset could
 * discard the event as belonging to the previous image.
 */
static void capacity_ble_settle(void) {
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        name_step();
        capacity_ble_require(naming && !name_closing && !name_client.retained,
                             "editor ended while settling its frame");
        if (!(name_state.flags & RISC_TEXT_ENTRY_PRESENTING)) return;
        runtime->yield_ms(1);
    }
    capacity_ble_require(false, "editor frame never settled");
}

static void capacity_ble_finish(void) {
    for (unsigned attempt = 0; attempt != 32 && naming && !name_client.retained;
         ++attempt) {
        name_step();
        if (naming && !name_client.retained) runtime->yield_ms(1);
    }
    capacity_ble_closed();
}

__attribute__((visibility("default"))) void app_main(void) {
    const char *mode = capacity_mode();
    if(mode&&!strcmp(mode,"entrypoint")){
        const t5_app_api_v1 *candidate=t5_app_get_api(1);
        capacity_ble_require(candidate&&candidate->screen_width()==480&&candidate->screen_height()==800,"X4 paper viewport unavailable");
        original_ble_main();
        capacity_ble_require(!naming&&!name_client.active&&!name_client.acquired,"240x240 entry guard unexpectedly admitted X4");
        capacity_phase("ble-entrypoint-geometry-refused");return;
    }
    bool accept = !mode || !*mode || capacity_ble_mode(mode, "accepted");
    bool pending = capacity_ble_mode(mode, "pending-close");
    bool retained = capacity_ble_mode(mode, "retained-close");
    bool handoff = capacity_ble_mode(mode, "handoff");
    bool back = capacity_ble_mode(mode, "back");
    bool navigation_back = capacity_ble_mode(mode, "navigation-back");
    bool cancel = capacity_ble_mode(mode, "cancelled");
    capacity_ble_require(accept || pending || retained || handoff || back ||
                         navigation_back || cancel, "unknown test mode");

    app = t5_app_get_api(1);
#ifdef CAPACITY_BLE_PAPER
    paper = paper_presentation_get();
    capacity_ble_require(paper != NULL, "selected BLE paper presentation unavailable");
#endif
    runtime = risc_runtime_get_api(1);
    capacity_ble_require(app && app->abi_version == 1 &&
                         app->struct_size >= offsetof(t5_app_api_v1, touch_contact) +
                                             sizeof(app->touch_contact) &&
                         app->poll && app->set_back_exits_app && runtime &&
                         runtime->api_version == 1 &&
                         runtime->struct_size >= RISC_RUNTIME_CAPABILITIES_V1_SIZE &&
                         runtime->acquire && runtime->release && runtime->yield_ms,
                         "real app adapter or Runtime unavailable");

    /* Seed one observed identity without starting RF.  Naming and persistence
     * are the unchanged production paths, including checked KV readback.
     */
    scan = (ble_scan){.phase = BLE_COMPLETE, .count = 1};
    scan.devices[0].address_type = 0;
    for (unsigned i = 0; i != 6; ++i) scan.devices[0].address[i] = (uint8_t)(i + 1);
    strcpy(scan.devices[0].name, "Capacity sensor");
    memset(aliases, 0, sizeof(aliases));
    memset(alias_state, 0, sizeof(alias_state));
    strcpy(aliases[0], "seed");
    alias_state[0] = 1;
    grant = (risc_runtime_capability_v1){0};
    host = NULL;
    token = 0;
    acquired = uncertain = sensors = restore_failed = naming = false;
    selected = scroll = detail_scroll = detail_index = 0;
    detail = dirty = true;
    message = "Capacity sensor";
    name_client = (portable_text_client){0};
    name_state = (risc_text_entry_state_v1){0};
    name_closing = name_save_failed = false;
    name_draft_index = 0;
    app->set_back_exits_app(false);
    capacity_phase("ble-initialized");

    begin_name();
    capacity_phase("ble-modal-open");
    capacity_ble_require(naming && name_client.active && name_client.acquired &&
                         name_client.suspended && name_client.session &&
                         !name_client.retained && !uncertain,
                         "real text-entry session did not open");
    capacity_ble_require(!portable_app_before_launch(PORTABLE_RETURN_APP),
                         "modal failed to fence application launch");
    capacity_ble_settle();

    capacity_control(CAPACITY_KEY_A);
    name_step();
    capacity_ble_require(naming && !name_client.retained &&
                         name_state.state == RISC_TEXT_ENTRY_EDITING &&
                         !strcmp(name_state.text, capacity_ble_expected()),
                         "real HID text path did not append a");
    capacity_phase("ble-modal-edited");

    if (retained) {
        capacity_ble_settle();
        capacity_control(CAPACITY_FAIL_UNSUBSCRIBE);
        capacity_control(CAPACITY_KEY_ESCAPE);
        name_step();
        capacity_ble_require(name_client.retained && uncertain && naming &&
                             name_client.acquired && name_client.suspended &&
                             name_client.session &&
                             portable_text_adapter_retained() &&
                             !portable_app_before_launch(PORTABLE_RETURN_APP),
                             "unsubscribe failure did not retain modal custody");
        capacity_ble_require(!strcmp(aliases[0], "seed"),
                             "retained cancellation changed the saved alias");
        capacity_phase("ble-modal-retained");
        /* Match production app_main: return without further provider access. */
        return;
    }

    if (pending) {
        const uint64_t session = name_client.session;
        const risc_runtime_capability_v1 held = name_client.grant;
        capacity_control(CAPACITY_DISPLAY_PENDING);
        if(capacity_hardware()){capacity_control(CAPACITY_KEY_ENTER);name_step();}else{name_step();capacity_ble_require(navigate_back(),"pending Back left app");}
        capacity_ble_require(naming && name_closing && name_client.active &&
                             name_client.closing && name_client.acquired &&
                             name_client.suspended && !name_client.retained &&
                             name_client.session == session &&
                             name_client.grant.api == held.api &&
                             name_client.grant.slot == held.slot &&
                             name_client.grant.generation == held.generation &&
                             name_state.state == (capacity_hardware()?RISC_TEXT_ENTRY_ACCEPTED:RISC_TEXT_ENTRY_CANCELLED) &&
                             !strcmp(aliases[0], "seed"),
                             "pending close lost custody or saved prematurely");
        capacity_phase("ble-close-pending");
        name_step();
        capacity_ble_require(naming && name_closing && !name_client.retained &&
                             name_client.session == session &&
                             name_client.grant.api == held.api &&
                             name_client.grant.slot == held.slot &&
                             name_client.grant.generation == held.generation,
                             "pending close retry changed modal custody");
        capacity_control(CAPACITY_DISPLAY_COMPLETE);
    } else if (handoff || navigation_back) {
        capacity_ble_settle();
        capacity_control(CAPACITY_NAVIGATION_BACK);
        if (handoff) {
            const uint64_t session = name_client.session;
            t5_app_input_t input = {.buttons = UINT32_MAX, .tapped = true,
                                    .exit_requested = true};
            bool polled = app->poll(&input, 0);
            capacity_ble_require(!polled && !input.buttons && !input.tapped &&
                                 !input.exit_requested && naming &&
                                 name_client.active && name_client.suspended &&
                                 name_client.session == session &&
                                 !portable_app_before_launch(PORTABLE_RETURN_APP),
                                 "real adapter permitted input/handoff during modal");
            capacity_phase("ble-handoff-fenced");
        }
    } else if (back) {
        bool stayed = navigate_back();
        capacity_ble_require(stayed, "modal Back tried to exit the application");
    } else {
        capacity_ble_settle();capacity_control(accept ? CAPACITY_KEY_ENTER : CAPACITY_KEY_ESCAPE);
    }

    capacity_ble_finish();
    capacity_ble_require(!name_save_failed &&
                         !strcmp(aliases[0], accept || (pending&&capacity_hardware()) ? capacity_ble_expected() : "seed"),
                         "accept/cancel did not preserve expected alias");
    capacity_ble_require(name_state.state == (accept || (pending&&capacity_hardware()) ?
                         RISC_TEXT_ENTRY_ACCEPTED : RISC_TEXT_ENTRY_CANCELLED),
                         "unexpected terminal text-entry state");
    capacity_phase("ble-modal-closed");

    if (handoff) {
        bool stayed = navigate_back();
        capacity_ble_require(stayed && !detail,
                             "Back did not leave sensor detail");
        stayed = navigate_back();
        capacity_ble_require(!stayed,
                             "clean client could not request launcher handoff");
        capacity_phase("ble-handoff-requested");
    }
    stop();
    app->set_back_exits_app(true);
    capacity_phase("ble-cleanup");
}

const t5_app_manifest_t portable_catalog[]={{.compatible=false}};
const unsigned portable_catalog_count=0;
