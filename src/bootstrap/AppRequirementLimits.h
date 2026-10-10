#pragma once
// Build-selected immutable declaration bound only; no additional live grants.
#ifndef RISC_APP_REQUIREMENT_ROWS
#define RISC_APP_REQUIREMENT_ROWS 16
#endif
#if RISC_APP_REQUIREMENT_ROWS != 16 && RISC_APP_REQUIREMENT_ROWS != 17
#error "RISC_APP_REQUIREMENT_ROWS must be 16 or 17"
#endif
#if RISC_APP_REQUIREMENT_ROWS == 17
#define RISC_APP_REQUIREMENT_ROWS_MARKER "RISC_APP_REQUIREMENT_ROWS:17"
#endif
