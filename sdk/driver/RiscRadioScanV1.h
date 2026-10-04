#pragma once
/* Bounded, asynchronous station scanning. App policy and credentials remain
 * outside the CPU port. This header adds no capability or hardware authority. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define GARDEN_RADIO_SCAN_MAX 16u
#define GARDEN_RADIO_SCAN_IDLE 0u
#define GARDEN_RADIO_SCAN_RUNNING 1u
#define GARDEN_RADIO_SCAN_DONE 2u
#define GARDEN_RADIO_SCAN_FAILED 3u
#define GARDEN_RADIO_AUTH_OPEN 0u
#define GARDEN_RADIO_AUTH_WPA_PSK 1u
#define GARDEN_RADIO_AUTH_WPA2_PSK 2u
#define GARDEN_RADIO_AUTH_WPA_WPA2_PSK 3u
#define GARDEN_RADIO_AUTH_WPA3_PSK 4u
#define GARDEN_RADIO_AUTH_WPA2_WPA3_PSK 5u
#define GARDEN_RADIO_AUTH_UNSUPPORTED 255u
/* SSID is at most 32 bytes, NUL terminated. Hidden SSIDs may be empty.
 * Authentication is descriptive only; unsupported entries cannot be joined.
 * No SDK record, BSSID, borrowed string or password crosses this boundary. */
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel, auth, reserved;
} garden_radio_scan_entry_v1;
typedef struct {
    uint32_t struct_size;
    uint8_t count, state;
    uint16_t reserved;
    garden_radio_scan_entry_v1 entries[GARDEN_RADIO_SCAN_MAX];
} garden_radio_scan_result_v1;
/* start never waits for RF completion; polls return copied bounded snapshots.
 * Repeated start while running and joining during scan are rejected. cancel is
 * idempotent; false retains ownership and requires successful cleanup/retry.
 * poll requires struct_size >= sizeof(result); reserved output is always zero. */
typedef bool (*garden_radio_scan_start_v1)(void*, uint64_t);
typedef bool (*garden_radio_scan_poll_v1)(void*, uint64_t, garden_radio_scan_result_v1*);
typedef bool (*garden_radio_scan_cancel_v1)(void*, uint64_t);
