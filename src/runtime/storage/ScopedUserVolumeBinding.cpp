#include "ScopedUserVolumeBinding.h"
#include <cstdio>
#include <cstring>

namespace RiscStorage {
namespace {
bool bounded(const char*s,size_t maximum){if(!s || !*s)return false;for(size_t i=0;i<maximum;++i)if(!s[i])return true;return false;}
bool identifier(const char*s){
 if(!bounded(s,95))return false;
 for(;*s;++s)if(!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') || (*s>='0' && *s<='9') || *s=='_' || *s=='-' || *s=='.'))return false;
 return true;
}
}
bool ScopedUserVolumeBinding::fail(const char*s){snprintf(error_,sizeof(error_),"%s",s);return false;}
bool ScopedUserVolumeBinding::retain(const char*s){retained_=true;return fail(s);}
bool ScopedUserVolumeBinding::owner(void*c){auto&s=*static_cast<ScopedUserVolumeBinding*>(c);return s.hooks_.owner && s.hooks_.owner(s.hooks_.context);}
bool ScopedUserVolumeBinding::safe(void*c){
 auto&s=*static_cast<ScopedUserVolumeBinding*>(c);
 return !s.retained_ && owner(c) && s.hooks_.safe && s.hooks_.safe(s.hooks_.context) &&
   s.graph_.activationSafe() && s.grant_.slot && s.graph_.interfaceFor(s.grant_)==s.upstream_;
}
bool ScopedUserVolumeBinding::configure(const char*provider,uint64_t instance,const char*root,const char*label){
 if(configured_ || busy_ || !exitSafe() || !owner(this) || !hooks_.safe || !identifier(provider) ||
    !ScopedUserVolume::validRoot(root) || !bounded(label,sizeof(label_)))return false;
 // Zero requires the unique named provider, never a capability-wide search.
 strcpy(provider_,provider);strcpy(root_,root);strcpy(label_,label);instance_=instance;configured_=true;return true;
}
bool ScopedUserVolumeBinding::release(){
 if(!grant_.slot)return true;
 if(!hooks_.safe(hooks_.context))return retain("Native storage custody lost. Keep binding and graph.");
 const bool released=graph_.release(grant_);
 if(!hooks_.safe(hooks_.context))return retain("Provider release custody lost. Keep binding and graph.");
 if(!released)return fail("Provider release pending. Keep graph; retry end only.");
 grant_={};upstream_=nullptr;return true;
}
bool ScopedUserVolumeBinding::begin(risc_storage_volume_api_v1_ext*out){
 if(!out || !configured_ || busy_ || !owner(this) || !exitSafe() || graph_.lifecycleBusy())return false;
 if(!hooks_.safe(hooks_.context) || !graph_.activationSafe())return fail("Storage provider admission unavailable");
 busy_=true;error_[0]=0;
 grant_=graph_.acquireFrom(provider_,"storage.volume",RISC_STORAGE_VOLUME_API_V1,instance_);
 if(!grant_.slot){
  if(!hooks_.safe(hooks_.context) || !graph_.activationSafe())retain("Provider activation retained. Keep binding and graph.");
  else fail(graph_.lastError());
  busy_=false;return false;
 }
 if(!hooks_.safe(hooks_.context) || !graph_.activationSafe()){
  retain("Provider activation custody lost. Keep binding and graph.");busy_=false;return false;
 }
 upstream_=static_cast<const risc_storage_volume_api_v1*>(graph_.interfaceFor(grant_));
 if(!upstream_){retain("Provider grant lost. Keep binding and graph.");busy_=false;return false;}
 volume_.emplace(Hooks{this,owner,safe});risc_storage_volume_api_v1_ext table{};
 const bool ok=volume_->configure(upstream_,root_,label_) && volume_->beginExtended(&table);
 if(!ok){
  if(volume_->retained())retain("Scoped volume admission retained. Keep binding and graph.");
  else {volume_.reset();if(release())fail("Scoped root or required provider operations unavailable");}
  busy_=false;return false;
 }
 *out=table;busy_=false;return true;
}
bool ScopedUserVolumeBinding::end(){
 if(busy_ || !owner(this) || retained_ || graph_.lifecycleBusy())return false;
 busy_=true;
 if(volume_){
  if(!volume_->end()){
   if(volume_->retained())retain("Scoped volume cleanup retained. Keep binding and graph.");
   busy_=false;return false;
  }
  volume_.reset();
 }
 const bool ok=release();if(ok)error_[0]=0;busy_=false;return ok;
}
}
