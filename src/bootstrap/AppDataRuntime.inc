bool Runtime::appDataExitSafe()const{return !port_.appData || (port_.appData->exitSafe && port_.appData->exitSafe(port_.appData->context));}
Runtime* Runtime::appDataContext(void*context){
 Runtime*r=currentRuntime;
 if(!r || !r->active() || r->retained_ || !context || context!=r->appDataContext_ || !r->port_.appData)return nullptr;
 for(const auto&grant:r->appGrants_)if(grant.live && grant.api==&r->appDataTable_)return r;
 return nullptr;
}
int32_t Runtime::appDataStat(void*context,const char*name,uint32_t*size,uint64_t*revision){
 if(size)*size=0;
 if(revision)*revision=0;
 Runtime*r=appDataContext(context);if(!r)return RISC_APP_DATA_CONTEXT;
 if(!r->appDataExitSafe())return RISC_APP_DATA_RETAINED;
 if(!r->providerStorageSafe())return RISC_APP_DATA_UNAVAILABLE;
 const auto&b=*r->port_.appData;return b.stat(b.context,r->appDataNamespace_,name,size,revision);
}
int32_t Runtime::appDataRead(void*context,const char*name,uint64_t expected,void*bytes,uint32_t capacity,uint32_t*size,uint64_t*revision){
 if(size)*size=0;
 if(revision)*revision=0;
 Runtime*r=appDataContext(context);if(!r)return RISC_APP_DATA_CONTEXT;
 if(!r->appDataExitSafe())return RISC_APP_DATA_RETAINED;
 if(!r->providerStorageSafe())return RISC_APP_DATA_UNAVAILABLE;
 const auto&b=*r->port_.appData;return b.read(b.context,r->appDataNamespace_,name,expected,bytes,capacity,size,revision);
}
int32_t Runtime::appDataReplace(void*context,const char*name,uint64_t expected,const void*bytes,uint32_t size){
 Runtime*r=appDataContext(context);if(!r)return RISC_APP_DATA_CONTEXT;
 if(!r->appDataExitSafe())return RISC_APP_DATA_RETAINED;
 if(!r->providerStorageSafe())return RISC_APP_DATA_UNAVAILABLE;
 const auto&b=*r->port_.appData;return b.replace(b.context,r->appDataNamespace_,name,expected,bytes,size);
}
