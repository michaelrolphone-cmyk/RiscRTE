#pragma once
#include <stdbool.h>
// Internal port-to-runtime lifetime barrier. It conveys no hardware semantics.
// If a port cannot prove quiescence, disposal must retain its caller/resources.
#if defined(RISCRTE_SD_SPI_FAULT_PORT) || defined(RISCRTE_SD_SPI_FAULT_TEST)
#ifdef __cplusplus
extern "C" {
#endif
bool risc_runtime_retention_required(void);
void risc_runtime_retention_guard(void);
#ifdef __cplusplus
}
#endif
#else
static inline bool risc_runtime_retention_required(void) { return false; }
static inline void risc_runtime_retention_guard(void) {}
#endif
