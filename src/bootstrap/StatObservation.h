#pragma once
#include <cstdint>
#include <climits>
namespace RiscBoot {
// One accepted native STAT only. States absent from the execution are INT32_MIN.
// Flags record only guards/calls actually reached; bound means the bound guard passed.
// metadataStatus uses the selected table's existing native result domain.
enum : uint32_t { StatBound=1u, StatRetained=2u, StatMetadata=4u, StatReady=8u, StatReadyTrue=16u };
struct StatObservation {
 uint32_t flags;
 int32_t preState,postState,finalState,metadataStatus;
 uint32_t stateCalls;
};
inline StatObservation emptyStatObservation(){return {0,INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN,0};}
static_assert(sizeof(StatObservation)==24,"STAT observation must remain bounded");
}
