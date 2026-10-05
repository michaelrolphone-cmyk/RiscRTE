#pragma once
#include "TWatchPlatformV1.h"
/* Additive table extension. TWatchPlatformV1.h and its exact prefix stay intact.
 * Require transport.struct_size >= sizeof(risc_hci_controller_status_v1).
 * No callbacks or packet consumption occur during status. Failed queries write
 * RETAINED. Session 0 proves OFF only when no logical/native session remains;
 * otherwise the exact live token is required. No implicit open or cleanup. */
enum {
    RISC_HCI_CONTROLLER_OFF = 0,
    RISC_HCI_CONTROLLER_ON = 1,
    RISC_HCI_CONTROLLER_RETAINED = 2
};
typedef struct {
    twatch_hci_controller_v1 transport;
    bool (*status)(void *context, uint64_t session, uint8_t *state);
} risc_hci_controller_status_v1;
