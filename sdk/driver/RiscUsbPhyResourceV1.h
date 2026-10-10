#pragma once
/* Provider-only native ownership lease for the internal USB PHY shared with
 * the boot console. Contains no USB descriptors, endpoints, SCSI or SD logic.
 * Claims fence native output/recovery and sleep/restart before returning. The
 * caller must stop all controller transfers/callbacks/DMA before release.
 * A false claim with a nonzero token or any false release retains custody.
 * Keep provider/dependencies mapped; never restore the native console early. */
#include <stdbool.h>
#include <stdint.h>
#define RISC_USB_PHY_RESOURCE_CAPABILITY "platform.usb.phy.resource"
#define RISC_USB_PHY_RESOURCE_API_V1 1u
#define RISC_USB_PHY_ESP32S3_OTG 1u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 uint32_t api_version,struct_size;
 void *context;
 uint32_t controller_kind,reserved;
 bool (*is_owner)(void *context);
 bool (*claim)(void *context,uint64_t *token);
 bool (*release)(void *context,uint64_t token);
} risc_usb_phy_resource_api_v1;
#ifdef __cplusplus
}
#endif
