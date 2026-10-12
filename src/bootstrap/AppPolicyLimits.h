#pragma once

// Native build option only. More immutable policy rows do not add live grants,
// manifest requirements, namespaces, providers or hardware authority.
#ifndef RISC_APP_POLICY_ROWS
#define RISC_APP_POLICY_ROWS 16
#endif
#if RISC_APP_POLICY_ROWS != 16 && RISC_APP_POLICY_ROWS != 17 && RISC_APP_POLICY_ROWS != 18 && RISC_APP_POLICY_ROWS != 24 && RISC_APP_POLICY_ROWS != 32
#error "RISC_APP_POLICY_ROWS must be 16, 17, 18 24 or 32"
#endif
#if RISC_APP_POLICY_ROWS == 32
#define RISC_APP_POLICY_ROWS_MARKER "RISC_APP_POLICY_ROWS:32"
#elif RISC_APP_POLICY_ROWS == 24
#define RISC_APP_POLICY_ROWS_MARKER "RISC_APP_POLICY_ROWS:24"
#elif RISC_APP_POLICY_ROWS == 18
#define RISC_APP_POLICY_ROWS_MARKER "RISC_APP_POLICY_ROWS:18"
#elif RISC_APP_POLICY_ROWS == 17
#define RISC_APP_POLICY_ROWS_MARKER "RISC_APP_POLICY_ROWS:17"
#else
#define RISC_APP_POLICY_ROWS_MARKER "RISC_APP_POLICY_ROWS:16"
#endif
