// storage.preferences@1 is a capability-selected facade. No product keys,
// storage paths, hardware identities, or application semantics belong here.
namespace {
constexpr uint32_t PreferenceMutations=16,PreferenceGroup=4096,PreferenceIntent=1024;
int32_t preferenceStatus(int32_t rc) {
  switch(rc) {
    case RISC_KEY_VALUE_OK:return RISC_ATOMIC_OK;
    case RISC_KEY_VALUE_NOT_FOUND:return RISC_ATOMIC_NOT_FOUND;
    case RISC_KEY_VALUE_BUFFER_SMALL:return RISC_ATOMIC_BUFFER_SMALL;
    case RISC_KEY_VALUE_BUSY:return RISC_ATOMIC_BUSY;
    default:return RISC_ATOMIC_IO;
  }
}
}
Runtime::PreferenceNamespace* Runtime::preferenceNamespace(uint32_t ns,bool create) {
  for(auto& n:preferenceNamespaces_)if(n.id==ns)return &n;
  if(create && ns)for(auto& n:preferenceNamespaces_)if(!n.id){n.id=ns;return &n;}
  return nullptr;
}
bool Runtime::preferenceAvailable(const char* app,uint32_t ns) {
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(atomicBinding(app,ns))return true;
  // A registered native namespace must never downgrade into an unrelated
  // synchronous writer, even when this app lacks native writer authority.
  for(size_t i=0;i<atomicBindingCount_;++i)if(atomicBindings_[i].nameSpace==ns)return false;
#else
  (void)app;
#endif
  return port_.keyValue && port_.keyValue->get && port_.keyValue->put &&
    port_.keyValue->maxBlobSize>=RISC_KEY_VALUE_BLOB_MAX && preferenceNamespace(ns,true);
}
bool Runtime::preferenceExitSafe(bool exiting) const {
  if(!exiting)return true;
  const auto safe=[](const Invocation& i){for(const auto& g:i.appGrants_)if(g.preferenceOperation)return false;return true;};
  return safe(mainInvocation_) && (!foreground_ || safe(*foreground_));
}
Runtime::AppGrant* Runtime::preferenceContext(void* context) {
  const auto owner=atomicKeyValueOwner.load(std::memory_order_acquire);
  if(!owner || !owner())return nullptr;
  auto* r=currentRuntime;
  if(!r || !context || !r->active() || r->retained_ || r->promotionRunning_ || r->graph_.lifecycleBusy() || !r->invocation_->entryRunning_)return nullptr;
  if(!r->providerStorageSafe()){(void)r->appExitBarrier(false);return nullptr;}
  for(auto& g:r->invocation_->appGrants_)if(g.live && g.invocation==r->invocation_->streams_.context() &&
     g.api==&g.preferences && g.preferences.context==context && g.preferenceNamespace)return &g;
  return nullptr;
}
int32_t Runtime::preferenceState(void* c,risc_preference_store_state_v1* out) {
  auto* g=preferenceContext(c);if(!g || !out || out->struct_size<sizeof(*out))return RISC_ATOMIC_INVALID;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(g->atomicEndpoint){const auto rc=atomicState(c,out);if(rc==RISC_ATOMIC_OK)out->flags|=RISC_PREFERENCE_STORE_COPIED_INPUT|RISC_PREFERENCE_STORE_ATOMIC_GROUP;return rc;}
#endif
  auto* r=currentRuntime;auto* n=r->preferenceNamespace(g->preferenceNamespace);
  if(!n)return RISC_ATOMIC_INVALID;
  if(n->pending)return RISC_ATOMIC_BUSY;
  const uint32_t limit=std::min(r->port_.keyValue->maxBlobSize,RISC_KEY_VALUE_V2_BLOB_MAX);
  *out={sizeof(*out),RISC_PREFERENCE_STORE_COPIED_INPUT,n->id,PreferenceMutations,limit,PreferenceGroup,PreferenceIntent,0,n->revision};
  return RISC_ATOMIC_OK;
}
int32_t Runtime::preferenceGet(void* c,uint64_t revision,const char* key,void* out,uint32_t capacity,uint32_t* size) {
  if(size)*size=0;
  auto* g=preferenceContext(c);if(!g || !size || !revision || !keyValueKey(key) || (!out && capacity))return RISC_ATOMIC_INVALID;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(g->atomicEndpoint)return atomicGet(c,revision,key,out,capacity,size);
#endif
  auto* r=currentRuntime;auto* n=r->preferenceNamespace(g->preferenceNamespace);
  if(!n)return RISC_ATOMIC_INVALID;
  if(n->pending)return RISC_ATOMIC_BUSY;
  if(revision!=n->revision)return RISC_ATOMIC_STALE;
  return preferenceStatus(r->namespaceKeyValueGet(n->id,key,out,capacity,size,std::min(r->port_.keyValue->maxBlobSize,RISC_KEY_VALUE_V2_BLOB_MAX)));
}
int32_t Runtime::preferenceSubmit(void* c,uint64_t revision,const risc_preference_store_mutation_v1* changes,uint32_t count,const void* intent,uint32_t intentSize,uint64_t* token) {
  if(token)*token=0;
  auto* g=preferenceContext(c);if(!g)return RISC_ATOMIC_INVALID;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(g->atomicEndpoint)return atomicSubmit(c,revision,changes,count,intent,intentSize,token);
#endif
  if(!token || !revision || (count && !changes) || count>PreferenceMutations || !intent || !intentSize || intentSize>PreferenceIntent)return RISC_ATOMIC_INVALID;
  auto* r=currentRuntime;auto* n=r->preferenceNamespace(g->preferenceNamespace);
  if(!n)return RISC_ATOMIC_INVALID;
  if(n->pending || g->preferenceOperation)return RISC_ATOMIC_BUSY;
  if(revision!=n->revision)return RISC_ATOMIC_STALE;
  if(n->revision>UINT64_MAX-count || r->preferenceOperationGeneration_==UINT64_MAX)return RISC_ATOMIC_CAPACITY;
  const uint32_t limit=std::min(r->port_.keyValue->maxBlobSize,RISC_KEY_VALUE_V2_BLOB_MAX);
  uint32_t bytes=intentSize;
  for(uint32_t i=0;i<count;++i){const auto& m=changes[i];
    if(!keyValueKey(m.key) || m.kind!=RISC_ATOMIC_PUT || !m.value || !m.size || m.size>limit)return RISC_ATOMIC_INVALID;
    for(uint32_t j=0;j<i;++j)if(!strcmp(changes[j].key,m.key))return RISC_ATOMIC_INVALID;
    if(m.size>PreferenceGroup-bytes || 24>PreferenceGroup-bytes-m.size)return RISC_ATOMIC_CAPACITY;
    bytes+=24+m.size;
  }
  // Copy all inputs before the first backend call. Heap payload avoids growing
  // the owner stack; only the receipt survives submit, and scratch is wiped.
  auto* payload=static_cast<uint8_t*>(std::malloc(bytes));if(!payload)return RISC_ATOMIC_CAPACITY;
  struct Scratch {uint8_t* p;uint32_t size;~Scratch(){volatile uint8_t* v=p;for(uint32_t i=0;i<size;++i)v[i]=0;std::free(p);}} scratch{payload,bytes};
  risc_preference_store_mutation_v1 copied[PreferenceMutations]{};
  uint32_t at=0;
  for(uint32_t i=0;i<count;++i){const auto& m=changes[i];
    memcpy(payload+at,m.key,strlen(m.key)+1);memcpy(payload+at+24,m.value,m.size);
    copied[i]={reinterpret_cast<const char*>(payload+at),RISC_ATOMIC_PUT,payload+at+24,m.size};at+=24+m.size;
  }
  memcpy(payload+at,intent,intentSize);
  n->pending=c;g->preferenceOperation=++r->preferenceOperationGeneration_;
  int32_t status=RISC_ATOMIC_OK;
  for(uint32_t i=0;i<count;++i){const auto& m=copied[i];
    if(r->namespaceKeyValuePut(n->id,m.key,m.value,m.size,true)!=RISC_KEY_VALUE_OK){status=RISC_ATOMIC_UNCERTAIN;break;}
  }
  g->preferenceReceipt={sizeof(g->preferenceReceipt),status,n->revision,status==RISC_ATOMIC_OK?g->preferenceOperation:0};
  *token=g->preferenceOperation;return RISC_ATOMIC_OK;
}
int32_t Runtime::preferenceCollect(void* c,uint64_t token,risc_preference_store_receipt_v1* out) {
  auto* g=preferenceContext(c);if(!g)return RISC_ATOMIC_INVALID;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(g->atomicEndpoint)return atomicCollect(c,token,out);
#endif
  if(!token || token!=g->preferenceOperation || !out || out->struct_size<sizeof(*out))return RISC_ATOMIC_INVALID;
  auto* n=currentRuntime->preferenceNamespace(g->preferenceNamespace);
  if(!n || n->pending!=c)return RISC_ATOMIC_INVALID;
  *out=g->preferenceReceipt;g->preferenceReceipt={};g->preferenceOperation=0;n->pending=nullptr;return RISC_ATOMIC_OK;
}
int32_t Runtime::preferenceCancel(void* c,uint64_t token) {
  auto* g=preferenceContext(c);if(!g)return RISC_ATOMIC_INVALID;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(g->atomicEndpoint)return atomicCancel(c,token);
#endif
  // Synchronous publication has already finished. Cancellation lost that race;
  // the original terminal receipt remains mandatory and is never rewritten.
  return token && token==g->preferenceOperation?RISC_ATOMIC_PENDING:RISC_ATOMIC_INVALID;
}
