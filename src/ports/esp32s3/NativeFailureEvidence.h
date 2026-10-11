#pragma once
#include "bootstrap/FailureEvidenceBackend.h"
#ifndef RISC_NATIVE_FAILURE_EVIDENCE
#define RISC_NATIVE_FAILURE_EVIDENCE 0
#endif
namespace RiscCpu { namespace NativeFailureEvidence {
#if RISC_NATIVE_FAILURE_EVIDENCE
void start();
const RiscBoot::FailureEvidenceBackend* backend();
#else
inline void start(){}
inline const RiscBoot::FailureEvidenceBackend* backend(){return nullptr;}
#endif
}}
