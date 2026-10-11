#pragma once
/* Interpreted by the app and serial provider adapter;
 * generic Runtime copies these bytes and never includes this header. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_SERIAL_STREAM_REQUEST_V1 1u
typedef struct {
  uint32_t baud_rate;
  uint8_t data_bits, parity, stop_bits, flow_control;
} risc_serial_stream_config_v1;
typedef struct {
  uint32_t api_version, struct_size;
  uint64_t provider_device, device_generation;
  risc_serial_stream_config_v1 config;
} risc_serial_stream_open_v1;
enum {
  RISC_SERIAL_STREAM_CONFIGURE=1,
  RISC_SERIAL_STREAM_CONTROL_LINES=2,
  RISC_SERIAL_STREAM_CHECK_DEVICE=3
};
typedef struct {
  uint32_t api_version, struct_size, operation, reserved;
  union {
    risc_serial_stream_config_v1 config;
    struct { uint8_t dtr, rts, reserved[6]; } lines;
    uint8_t zero[8];
  } value;
} risc_serial_stream_call_v1;
/* All three calls return status only, with reply_size=0. CHECK_DEVICE requires
 * a complete valid inventory: exact device+generation => OK; valid absence =>
 * DISCONNECTED; failed/malformed/partial inventory => IO, preserving custody.
 */
#ifdef __cplusplus
}
#endif
