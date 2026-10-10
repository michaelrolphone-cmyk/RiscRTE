#pragma once
/* Additive copied asynchronous lifecycle. No provider/application pointers are
 * retained. ACCEPTED/PENDING/AGAIN are progress, never cleanup failure. */
#include "RiscRadioScanV1.h"
#define RISC_RADIO_ASYNC_TAG 0x52415331u
#define RISC_RADIO_ASYNC_VERSION 1u
#define RISC_RADIO_REQUEST_SCAN 1u
#define RISC_RADIO_REQUEST_JOIN 2u
#define RISC_RADIO_ACCEPTED 0
#define RISC_RADIO_PENDING 1
#define RISC_RADIO_QUIESCENT 2
#define RISC_RADIO_AGAIN 3
#define RISC_RADIO_BUSY (-1)
#define RISC_RADIO_INVALID (-2)
#define RISC_RADIO_UNAVAILABLE (-3)
#define RISC_RADIO_STALE (-4)
#define RISC_RADIO_RETAINED (-5)
#define RISC_RADIO_QUEUED 1u
#define RISC_RADIO_STARTING 2u
#define RISC_RADIO_SCANNING 3u
#define RISC_RADIO_JOINING 4u
#define RISC_RADIO_CONNECTED 5u
#define RISC_RADIO_RESULTS 6u
#define RISC_RADIO_STOPPING 7u
#define RISC_RADIO_IDLE 8u
#define RISC_RADIO_CLEANUP_FAILED 9u
#define RISC_RADIO_FAILURE_NONE 0u
#define RISC_RADIO_FAILURE_SETUP 1u
#define RISC_RADIO_FAILURE_SCAN 2u
#define RISC_RADIO_FAILURE_LINK 3u
#define RISC_RADIO_FAILURE_CLEANUP 4u
#define RISC_RADIO_FAILURE_CANCELLED 5u
typedef struct {
    uint32_t struct_size, kind, flags;
    char ssid[33], password[64];
    uint8_t reserved[3];
} risc_radio_request_v1;
typedef struct {
    uint32_t struct_size, operation_id, phase, failure;
    uint8_t owned, quiescent, station_state;
    int8_t rssi;
    uint8_t station[12], reserved[4];
    garden_radio_scan_result_v1 scan;
} risc_radio_progress_v1;
/* Fixed size/tag native table. provision() occurs before admission is exposed,
 * never inside begin/poll/cancel. sharedReady is a copied readiness predicate
 * for PHY-sensitive lifecycle changes and entropy, not a storage-context gate. */
typedef struct {
    uint32_t struct_size, tag, version;
    int32_t (*begin)(const risc_radio_request_v1*,uint32_t*);
    int32_t (*poll)(uint32_t,risc_radio_progress_v1*);
    int32_t (*cancel)(uint32_t);
    bool (*sharedReady)(void);
    bool (*custodySafe)(void); // false only for returned cleanup uncertainty
    bool (*tryShared)(void); // bounded owner PHY/flash lease; false = defer
    void (*endShared)(void);
} risc_native_radio_async_v1;
