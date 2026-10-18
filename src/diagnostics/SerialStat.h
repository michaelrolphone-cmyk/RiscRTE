#pragma once
#include "bootstrap/StatObservation.h"
#include <cstdio>
#include <type_traits>
namespace RiscDiagnostics {
// Owner-only copies of completed, accepted STAT calls. No path/payload/pointers.
// Namespace zero means absent. Cached replies do not create another record.
struct SerialStatRecord {
 uint64_t nameSpace;
 RiscBoot::StatObservation decision;
 uint32_t session,sequence;
 int32_t status;
 uint32_t ms;
};
struct SerialStatSnapshot {SerialStatRecord current,firstRetained;};
static_assert(sizeof(SerialStatSnapshot)==96 && std::is_trivial<SerialStatSnapshot>::value && std::is_standard_layout<SerialStatSnapshot>::value,"STAT snapshot must remain bounded POD");
inline void observeStat(SerialStatSnapshot& out,uint32_t ms,uint32_t sid,uint32_t seq,uint64_t ns,int32_t status,const RiscBoot::StatObservation& decision){
 out.current={ns,decision,sid,seq,status,ms};
 // RISC_FILES_RETAINED. This latch is evidence only, never a custody decision.
 if(status==-6&&!out.firstRetained.nameSpace)out.firstRetained=out.current;
}
inline unsigned statLines(const SerialStatSnapshot& s){return (s.current.nameSpace?2u:0u)+(s.firstRetained.nameSpace?2u:0u);}
inline int statLine(const SerialStatSnapshot& s,unsigned part,char*out,size_t capacity){
 const bool first=part>1;const auto&r=first?s.firstRetained:s.current;const char*label=first?"first-retained":"current";
 if(!(part&1))return std::snprintf(out,capacity,"RTE_DIAG stat %s ns=%llu session=%08lx sequence=%lu status=%ld ms=%lu\n",label,(unsigned long long)r.nameSpace,(unsigned long)r.session,(unsigned long)r.sequence,(long)r.status,(unsigned long)r.ms);
 const auto&d=r.decision;
 return std::snprintf(out,capacity,"RTE_DIAG stat %s flags=%lu pre=%ld metadata=%ld post=%ld final=%ld state_calls=%lu\n",label,(unsigned long)d.flags,(long)d.preState,(long)d.metadataStatus,(long)d.postState,(long)d.finalState,(unsigned long)d.stateCalls);
}
}
