#pragma once
/* Firmware composition policy, never an app grant. Ports must explicitly opt
 * in after qualifying native allocations that do not use pressure retry paths. */
#ifndef RISC_APP_IMAGE_CACHE
#define RISC_APP_IMAGE_CACHE 0
#endif
