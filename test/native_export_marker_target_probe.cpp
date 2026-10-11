#include "experimental/native_replica/NativeReplicaCore.h"
using C=RiscExperimental::NativeReplicaCore;
static_assert(C::Header==128&&C::HeaderV2==384&&C::MarkerBytes==256,"native image format");
static_assert(sizeof(C::ExportMarker)==256,"opaque marker handle size");
int nativeExportMarkerTargetProbe(C&core,C::Snapshot&snapshot,const C::Identity&identity,const uint8_t*fresh){
 C::ExportMarker marker;
 int rc=core.captureSnapshot(&snapshot);if(rc)return rc;
 rc=core.prepareExport(snapshot,identity,fresh,&marker);if(rc)return rc;
 rc=core.preparedExport(&marker);if(rc)return rc;
 if(!marker.valid()||!core.exportPrepared()||core.formatVersion()!=2||core.headerSize()!=C::HeaderV2)return C::Invalid;
 if(snapshot.headerSize()!=C::Header&&snapshot.headerSize()!=C::HeaderV2)return C::Invalid;
 if(marker.namespaceId()!=core.namespaceId()||marker.through()<marker.previousAck())return C::Invalid;
 return identity.generation==marker.target().generation?core.acknowledgeExport(marker,identity):core.retireExport(marker,identity);
}
