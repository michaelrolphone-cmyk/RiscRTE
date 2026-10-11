// Synthetic opaque values only. No device data, SSIDs or passwords.
static std::map<uint32_t,std::map<std::string,std::string>> sharedOriginal,sharedDeltas;
static std::map<uint32_t,std::vector<uint8_t>> sharedAnchors,sharedDisk;
static const risc_bound_key_value_v1* sharedProvider;
static unsigned sharedPass=0;
extern "C" void removable_bound_settings(const void*api){sharedProvider=static_cast<const risc_bound_key_value_v1*>(api);}
static RiscStorage::ScalableSettings::Backend sharedBackend{nullptr,
 [](void*,uint32_t ns,void*out,uint32_t cap,uint32_t*n)->int32_t{auto&p=sharedAnchors[ns];if(p.empty())return RISC_KEY_VALUE_NOT_FOUND;assert(cap>=p.size());*n=p.size();memcpy(out,p.data(),*n);return 0;},
 [](void*,uint32_t ns,const void*p,uint32_t n)->int32_t{sharedAnchors[ns].assign((const uint8_t*)p,(const uint8_t*)p+n);return 0;},
 [](void*,uint32_t ns,bool delta,const char*after,char key[16],void*out,uint32_t cap,uint32_t*n)->int32_t{
  auto&m=delta?sharedDeltas[ns]:sharedOriginal[ns];auto it=m.upper_bound(after);if(it==m.end())return RISC_KEY_VALUE_NOT_FOUND;
  strcpy(key,it->first.c_str());*n=it->second.size();assert(cap>=*n);memcpy(out,it->second.data(),*n);return 0;},
 [](void*,uint32_t ns,const char*k,const void*p,uint32_t n)->int32_t{sharedDeltas[ns][k].assign((const char*)p,n);return 0;},
 [](void*,uint32_t ns,const char*k)->int32_t{sharedDeltas[ns].erase(k);return 0;}};
static RiscBoot::KeyValueBackend sharedKv{nullptr,
 [](void*,uint32_t ns,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{
  assert(ns==7);auto it=sharedOriginal[ns].find(k);if(it==sharedOriginal[ns].end())return RISC_KEY_VALUE_NOT_FOUND;
  *n=it->second.size();if(cap<*n)return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(out,it->second.data(),*n);return 0;},
 [](void*,uint32_t ns,const char*k,const void*p,uint32_t n)->int32_t{assert(ns==7);sharedOriginal[ns][k].assign((const char*)p,n);return 0;},
 2048,
 [](void*,uint32_t,void*,uint32_t,uint32_t*)->int32_t{return RISC_KEY_VALUE_NOT_FOUND;},
 [](void*,uint32_t,const void*,uint32_t)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},
 [](void*,uint32_t,bool*exists)->int32_t{*exists=false;return 0;},&sharedBackend};
static void sharedSettingsTest(){
 const auto*rt=risc_runtime_get_api(1);assert(rt&&installer&&sharedProvider);
 assert(installer->shared_settings_count==2&&installer->settings_capacity==128);
 assert(installer->shared_settings_namespace(installer->context,0)==1);
 assert(installer->shared_settings_namespace(installer->context,1)==6);
 assert(!installer->shared_settings_namespace(installer->context,2));
 assert(installer->settings_status(installer->context,131)==UINT32_MAX);
 risc_runtime_capability_v1 grants[3]{};const risc_key_value_v1*kv[3]{};
 const uint32_t namespaces[]={1,6,7};
 for(unsigned i=0;i<3;++i){grants[i].struct_size=sizeof(grants[i]);assert(rt->acquire("storage.key-value",1,namespaces[i],&grants[i]));kv[i]=static_cast<const risc_key_value_v1*>(grants[i].api);}
 char out[64]{};uint32_t n=0;
 for(unsigned i=0;i<2;++i){
  const uint32_t ns=namespaces[i],slot=129+i;char owner[32];snprintf(owner,sizeof owner,"shared-%08x",unsigned(ns));
  assert(kv[i]->get(kv[i]->context,"same",out,sizeof out,&n)==RISC_KEY_VALUE_BUSY);
  assert(kv[i]->put(kv[i]->context,"same","premature",9)==RISC_KEY_VALUE_BUSY);
  assert(installer->settings_restore(installer->context,slot,"wrong-owner",nullptr,0,true)==RISC_KEY_VALUE_CONTEXT);
  auto&disk=sharedDisk[ns];int rc=0;unsigned steps=0;
  do{rc=installer->settings_restore(installer->context,slot,owner,disk.empty()?nullptr:disk.data(),disk.size(),disk.empty());assert(++steps<10000);}while(rc==RISC_KEY_VALUE_BUSY);
  assert(rc==0);
  if(sharedPass){assert(kv[i]->get(kv[i]->context,"same",out,sizeof out,&n)==0&&std::string(out,n)==(i?"six":"one"));}
  else assert(kv[i]->put(kv[i]->context,"same",i?"six":"one",3)==0);
 }
 for(unsigned i=0;i<40;++i){char key[16];snprintf(key,sizeof key,"legacy%04u",i);assert(kv[1]->get(kv[1]->context,key,out,sizeof out,&n)==0&&std::string(out,n)=="opaque-legacy");}
 // App and provider routes must see the same shared namespace.
 if(!sharedPass){
  assert(sharedProvider->put(sharedProvider->context,"wifi.test","synthetic",9)==0);
  assert(kv[1]->get(kv[1]->context,"wifi.test",out,sizeof out,&n)==0&&std::string(out,n)=="synthetic");
  assert(kv[1]->put(kv[1]->context,"wifi.test","updated",7)==0);
 }
 assert(sharedProvider->get(sharedProvider->context,"wifi.test",out,sizeof out,&n)==0&&std::string(out,n)=="updated");
 assert(kv[0]->get(kv[0]->context,"wifi.test",out,sizeof out,&n)==RISC_KEY_VALUE_NOT_FOUND);
 // Existing profiles can exceed sixteen keys. Every synthetic record survives
 // the native erase, without a profile count or fixed key-array ceiling.
 for(unsigned i=0;i<1024;++i){char key[16];snprintf(key,sizeof key,"profile%04u",i);std::string value="opaque-"+std::to_string(i);
  if(sharedPass)assert(kv[1]->get(kv[1]->context,key,out,sizeof out,&n)==0&&std::string(out,n)==value);
  else assert(kv[1]->put(kv[1]->context,key,value.data(),value.size())==0);
 }
 // A namespace omitted from shared_settings stays on the original backend.
 assert(kv[2]->put(kv[2]->context,"same","native",6)==0);
 assert(kv[2]->get(kv[2]->context,"same",out,sizeof out,&n)==0&&std::string(out,n)=="native");
 for(unsigned i=0;i<2;++i){
  const uint32_t ns=namespaces[i],slot=129+i;uint64_t generation=0;uint32_t size=0;
  assert(installer->settings_snapshot(installer->context,slot,nullptr,0,&size,&generation)==RISC_KEY_VALUE_BUFFER_SMALL);
  auto&disk=sharedDisk[ns];disk.resize(size);
  assert(installer->settings_snapshot(installer->context,slot,disk.data(),size,&size,&generation)==0);
  int rc;do{rc=installer->settings_ack(installer->context,slot,generation);}while(rc==RISC_KEY_VALUE_BUSY);
  assert(!rc&&installer->settings_status(installer->context,slot)==1&&sharedDeltas[ns].empty());
  if(sharedPass){
   char owner[32];snprintf(owner,sizeof owner,"shared-%08x",unsigned(ns));
   // Once anchored, absent/corrupt SD may never resurrect the original NVS.
   assert(installer->settings_restore(installer->context,slot,owner,nullptr,0,true)==RISC_KEY_VALUE_IO);
   assert(kv[i]->get(kv[i]->context,"same",out,sizeof out,&n)==RISC_KEY_VALUE_BUSY);
   do{rc=installer->settings_restore(installer->context,slot,owner,disk.data(),disk.size(),false);}while(rc==RISC_KEY_VALUE_BUSY);assert(!rc);
  }
 }
 for(auto&grant:grants)assert(rt->release(&grant));
}
