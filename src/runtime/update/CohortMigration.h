#pragma once
#include "Cohort.h"
#include "runtime/RuntimeLimits.h"
namespace RiscUpdate {
static_assert(RiscLimits::Apps<32,"migration consumed mask must cover the entry bound");
// Source-bound syntax only. Authority is checked against both actual cohort
// identities and the running app policies by Runtime::validateCohort.
inline bool validCohortMigration(JsonVariantConst value){
  using namespace RiscBoot;
  if(value.isUnbound())return true;
  JsonObjectConst migration=value;int64_t number;char id[96],version[32],revision[41];uint32_t parts[3];
  if(!keys(migration,{"schema","from","to","shared_key_value"}) ||
     !integer(migration["schema"],1,1,number))return false;
  JsonObjectConst from=migration["from"],to=migration["to"];
  if(!keys(from,{"product","version","source_revision"}) || !keys(to,{"product","version"}) ||
     !text(from["product"],id,sizeof(id)) || !RuntimePackages::safeId(id) ||
     !text(from["version"],version,sizeof(version)) || !parseVersion(version,parts) ||
     !text(from["source_revision"],revision,sizeof(revision)) || !hex(revision,40) ||
     !text(to["product"],id,sizeof(id)) || !RuntimePackages::safeId(id) ||
     !text(to["version"],version,sizeof(version)) || !parseVersion(version,parts) ||
     from["product"]!=to["product"])return false;
  JsonArrayConst entries=migration["shared_key_value"];
  if(entries.isNull() || !entries.size() || entries.size()>RiscLimits::Apps)return false;
  for(size_t i=0;i<entries.size();++i){JsonObjectConst entry=entries[i];
    if(!keys(entry,{"application_id","api","namespace"}) ||
       !text(entry["application_id"],id,sizeof(id)) || !RuntimePackages::safeId(id) ||
       !integer(entry["api"],1,2,number) || !integer(entry["namespace"],1,INT32_MAX,number))return false;
    for(size_t j=0;j<i;++j)if(eq(entries[j]["application_id"],id) &&
       entries[j]["api"]==entry["api"] && entries[j]["namespace"]==entry["namespace"])return false;
  }
  return true;
}
inline bool migrationTarget(JsonObjectConst target,const CohortIdentity& identity){
  return RiscBoot::eq(target["product"],identity.product.product) && RiscBoot::eq(target["version"],identity.product.version);
}
inline bool sameCohortIdentity(const CohortIdentity& a,const CohortIdentity& b){
  return !strcmp(a.product.product,b.product.product) && !strcmp(a.product.version,b.product.version) &&
    !strcmp(a.product.source_repo,b.product.source_repo) && !strcmp(a.product.source_revision,b.product.source_revision) &&
    !strcmp(a.runtimeVersion,b.runtimeVersion) && !strcmp(a.layout,b.layout) && a.storeAbi==b.storeAbi &&
    a.firmwareSize==b.firmwareSize && !memcmp(a.firmwareSha,b.firmwareSha,32);
}
}
