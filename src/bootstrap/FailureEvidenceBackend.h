#pragma once
#include <RiscFailureEvidenceV1.h>
namespace RiscBoot {
// Optional firmware-owned sink. No app supplied hooks or retained pointers.
// Capture callbacks perform bounded memory-only work even after retention.
struct FailureEvidenceBackend {
  int32_t (*read)(risc_failure_evidence_v1*);
  int32_t (*acknowledge)(uint32_t boot,uint32_t sequence);
  void (*breadcrumb)(const char* application,uint64_t invocation,uint32_t phase,uint32_t role);
  void (*retained)(int32_t status,const char* detail);
};
}
