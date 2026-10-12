// Full concrete default-off assembly. Host seams are SDK NVS/RTOS, entropy,
// address-space classification and physical flash/SD; no factory/worker doubles.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main lower_port_main
#include "native_sd_port_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "ports/esp32s3/NativeAppDataMount.h"
#include "ports/esp32s3/NativeSettingsBackend.h"
#include "ports/esp32s3/NativeSettingsStartupEntropy.h"
#include "ports/esp32s3/NativeSettingsAssembly.h"
#include "ports/esp32s3/NativeSettingsWorker.h"
#include "native_littlefs_profile_fixture.h"
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <map>
#include <set>
#include <vector>
#include <fstream>
#include <atomic>
#include <filesystem>
namespace M=RiscAppDataMount;
namespace B=RiscCpu::NativeSettingsBackend;
namespace F=RiscCpu::NativeSettingsAssembly;
namespace W=RiscCpu::NativeSettingsWorker;
namespace L=RiscCpu::NativeSettingsLifecycle;
using C=RiscExperimental::NativeReplicaCore;
using Bytes=std::vector<uint8_t>;
static unsigned nvsCalls=0;
namespace Shim {
thread_local char anchor;
thread_local void*handle=&anchor;
struct Stop{};
struct TaskControl {std::thread thread;std::mutex mutex;std::condition_variable cv;bool stopping=false,parked=false;unsigned tickets=0,cycles=0;};
thread_local TaskControl*current=nullptr;
static std::vector<std::unique_ptr<TaskControl>>tasks;
static std::mutex memory;
struct Block {size_t size;unsigned caps;};
static std::map<void*,Block>blocks;static size_t livePayload=0,peakPayload=0,liveInternal=0,peakInternal=0;
static std::atomic<bool>lease{false};
static void tick(){for(auto&t:tasks){std::unique_lock<std::mutex>g(t->mutex);t->cv.wait(g,[&]{return t->parked;});auto before=t->cycles;++t->tickets;t->cv.notify_all();t->cv.wait(g,[&]{return t->parked&&t->cycles>before;});}}
static void stop(){for(auto&t:tasks){{std::lock_guard<std::mutex>g(t->mutex);t->stopping=true;}t->cv.notify_all();}for(auto&t:tasks)t->thread.join();}
}
extern "C" void*xTaskGetCurrentTaskHandle(){return Shim::handle;}
extern "C" int xPortInIsrContext(){return 0;}
extern "C" void vTaskDelay(unsigned){if(!Shim::current){std::this_thread::yield();return;}auto&t=*Shim::current;std::unique_lock<std::mutex>g(t.mutex);t.parked=true;++t.cycles;t.cv.notify_all();t.cv.wait(g,[&]{return t.tickets||t.stopping;});t.parked=false;if(t.stopping)throw Shim::Stop{};--t.tickets;g.unlock();std::this_thread::yield();}
extern "C" TaskHandle_t xTaskCreateStatic(void(*entry)(void*),const char*name,uint32_t bytes,void*arg,unsigned priority,StackType_t*,StaticTask_t*tcb){assert(!strcmp(name,"native-settings")&&bytes==W::StackBytes&&priority==1);auto t=std::make_unique<Shim::TaskControl>();auto*p=t.get();Shim::tasks.push_back(std::move(t));p->thread=std::thread([=]{task=Task::NativeSettings;Shim::handle=tcb;Shim::current=p;try{vTaskDelay(1);entry(arg);}catch(Shim::Stop&){};});return tcb;}
void*heap_caps_malloc(size_t n,unsigned caps){void*p=malloc(n);if(p){std::lock_guard<std::mutex>g(Shim::memory);Shim::blocks[p]={n,caps};if(caps&MALLOC_CAP_SPIRAM){Shim::livePayload+=n;Shim::peakPayload=std::max(Shim::peakPayload,Shim::livePayload);}else{Shim::liveInternal+=n;Shim::peakInternal=std::max(Shim::peakInternal,Shim::liveInternal);}}return p;}
void heap_caps_free(void*p){if(!p)return;std::lock_guard<std::mutex>g(Shim::memory);auto it=Shim::blocks.find(p);assert(it!=Shim::blocks.end());if(it->second.caps&MALLOC_CAP_SPIRAM)for(size_t i=0;i<it->second.size;++i)assert(!static_cast<uint8_t*>(p)[i]);if(it->second.caps&MALLOC_CAP_SPIRAM)Shim::livePayload-=it->second.size;else Shim::liveInternal-=it->second.size;Shim::blocks.erase(it);free(p);}
bool esp_ptr_external_ram(const void*p){std::lock_guard<std::mutex>g(Shim::memory);for(auto&v:Shim::blocks)if((v.second.caps&MALLOC_CAP_SPIRAM)&&uintptr_t(p)>=uintptr_t(v.first)&&uintptr_t(p)-uintptr_t(v.first)<v.second.size)return true;return false;}
bool esp_ptr_internal(const void*p){return p&&!esp_ptr_external_ram(p);}
namespace RiscNvs {esp_err_t initializationStatus(){return ESP_OK;}}
namespace RiscCpu {
bool nativeBackgroundResourceTry(){assert(owner());bool expected=false;return Shim::lease.compare_exchange_strong(expected,true);}
void nativeBackgroundResourceEnd(){assert(owner()&&Shim::lease.exchange(false));}
}
extern "C" void native_lfs_fixture_select_appdata(void);
static bool entropyWindow=false,entropyFail=false;static unsigned entropyFills=0;
extern "C" void bootloader_random_enable(){assert(!entropyWindow);entropyWindow=true;}
extern "C" void bootloader_random_disable(){assert(entropyWindow);entropyWindow=false;}
extern "C" void esp_fill_random(void*p,size_t n){assert(entropyWindow&&n==32);++entropyFills;memset(p,entropyFail?0:0x37,n);}
static std::map<std::string,std::map<std::string,Bytes>> nvs;
struct Handle {std::string space;nvs_open_mode_t mode;};
static std::map<nvs_handle_t,Handle>handles;
static nvs_handle_t nextHandle=1;
static void safe(){++nvsCalls;assert(task==Task::NativeSettings&&M::held());}
extern "C" esp_err_t nvs_open(const char*n,nvs_open_mode_t m,nvs_handle_t*out){safe();if(m==NVS_READONLY&&!nvs.count(n))return ESP_ERR_NVS_NOT_FOUND;*out=nextHandle++;handles[*out]={n,m};return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){safe();assert(handles.count(h));auto&space=nvs[handles[h].space];if(!space.count(k))return ESP_ERR_NVS_NOT_FOUND;auto&b=space[k];if(!out){*n=b.size();return ESP_OK;}if(*n<b.size())return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,b.data(),b.size());*n=b.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*b,size_t n){safe();assert(handles.count(h)&&handles[h].mode==NVS_READWRITE);auto*p=static_cast<const uint8_t*>(b);nvs[handles[h].space][k]=Bytes(p,p+n);return ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){safe();assert(handles.count(h));return ESP_OK;}
extern "C" void nvs_close(nvs_handle_t h){safe();assert(handles.erase(h)==1);}
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){safe();assert(handles.count(h));*n=nvs[handles[h].space].size();return ESP_OK;}
struct Iterator {std::string space;std::vector<std::string>keys;size_t at=0;};
static std::set<Iterator*>iterators;
extern "C" nvs_iterator_t nvs_entry_find(const char*p,const char*n,nvs_type_t t){safe();assert(!strcmp(p,"nvs")&&t==NVS_TYPE_ANY);if(!nvs.count(n)||nvs[n].empty())return nullptr;auto*i=new Iterator;i->space=n;for(auto&v:nvs[n])i->keys.push_back(v.first);iterators.insert(i);return i;}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t v){safe();auto*i=static_cast<Iterator*>(v);assert(iterators.count(i));if(++i->at<i->keys.size())return i;iterators.erase(i);delete i;return nullptr;}
extern "C" void nvs_entry_info(nvs_iterator_t v,nvs_entry_info_t*out){safe();auto*i=static_cast<Iterator*>(v);assert(iterators.count(i));*out={};strcpy(out->namespace_name,i->space.c_str());strcpy(out->key,i->keys[i->at].c_str());out->type=NVS_TYPE_BLOB;}
extern "C" void nvs_release_iterator(nvs_iterator_t v){safe();auto*i=static_cast<Iterator*>(v);if(i){assert(iterators.erase(i));delete i;}}

static std::string mode,output,cardPath;static Pin*anchorPin;static bool storageFinished=false;static void finishStorage();static void saveNative();
static const L::Admission*admission;
static C*core(unsigned ns){return ns==1?admission->preferences.core:admission->wifi.core;}
static void(*loadMedia)(const char*);static void(*saveMedia)(const char*);static void(*crashMedia)(const char*);
extern "C" void native_settings_sd_crash_media(void(*snapshot)(const char*)){crashMedia=snapshot;}
extern "C" void native_settings_sd_media(void(*load)(const char*),void(*save)(const char*)){loadMedia=load;saveMedia=save;}
static bool firstCut=false,publicationCut=false;static unsigned priorReplaces=0;static void schedule(uint32_t ms){assert(owner());nowMs+=ms;bool ranSd=false;if(!mailbox.idle()){task=Task::ProviderWorker;ranSd=mailbox.workerStep();task=Task::Owner;}Shim::tick();if(publicationCut&&ranSd&&f->counter(7)>priorReplaces){M::Guard g(M::Access::AppData);assert(g.held&&core(1)->exportPrepared()&&core(1)->identity().generation==5&&core(1)->pendingCount()==1);saveNative();crashMedia(cardPath.c_str());puts("POWER CUT after completed SD index replacement, before collection/native acknowledgement");fflush(stdout);std::_Exit(75);}if(firstCut&&core(1)->exportPrepared()){M::Guard cutGuard(M::Access::AppData);if(!cutGuard.held)return;C::ExportMarker marker;assert(!core(1)->preparedExport(&marker)&&marker.firstCreation());auto target=marker.target();std::ofstream saved(output+"/first-target.bin",std::ios::binary);saved.write((char*)&target,sizeof(target));saved.close();assert(saved.good());saveNative();saveMedia(cardPath.c_str());puts("POWER CUT after durable firstCreation marker, before first SD publication");fflush(stdout);std::_Exit(74);}}
static uint32_t availability(unsigned ns){risc_key_value_availability_state_v1 s{sizeof(s)};assert(!L::availability(ns,&s));return s.availability;}
static void settle(){const auto*api=risc_runtime_get_api(1);assert(api);for(unsigned i=0;i<300000&&L::continuing();++i)api->yield_ms(1);assert(L::exitSafe());}
static void discovery(){for(unsigned i=0;i<300000;++i){assert(L::discoveryCheckpoint());if(!L::discovering()&&L::exitSafe())return;schedule(1);}assert(false);}
static void value(C&c,const char*k,const std::string&v){char out[128]{};uint32_t n=0;assert(!c.get(k,out,sizeof(out),&n)&&n==v.size()&&!memcmp(out,v.data(),n));}
static void verify(unsigned ns,unsigned pass){auto&c=*core(ns);if(ns==1){value(c,"ui.theme",pass%2?"dark":"light");value(c,"ui.font",std::to_string(16+pass));value(c,"clock.zone","Europe/London");}else{for(unsigned i=0;i<3;++i)value(c,("wifi."+std::to_string(i)).c_str(),"synthetic-profile-"+std::to_string(i)+"-rev-"+std::to_string(pass));value(c,"wifi.selected",std::to_string(pass%3));}}
static void edit(const risc_key_value_atomic_v1&e,unsigned ns,unsigned pass){risc_key_value_atomic_state_v1 state{sizeof(state)};assert(!e.state(e.context,&state));std::vector<std::string>keys,values;if(ns==1){keys={"ui.theme","ui.font","clock.zone"};values={pass%2?"dark":"light",std::to_string(16+pass),"Europe/London"};}else{for(unsigned i=0;i<3;++i){keys.push_back("wifi."+std::to_string(i));values.push_back("synthetic-profile-"+std::to_string(i)+"-rev-"+std::to_string(pass));}keys.push_back("wifi.selected");values.push_back(std::to_string(pass%3));}std::vector<risc_key_value_atomic_mutation_v1>changes;for(unsigned i=0;i<keys.size();++i)changes.push_back({keys[i].c_str(),RISC_ATOMIC_PUT,values[i].data(),uint32_t(values[i].size())});uint64_t token=0;assert(!e.submit(e.context,state.revision,changes.data(),changes.size(),"explicit-host-edit",18,&token));risc_key_value_atomic_receipt_v1 result{sizeof(result)};int rc;do{schedule(1);rc=e.collect(e.context,token,&result);}while(rc==RISC_ATOMIC_PENDING);assert(!rc&&!result.status);verify(ns,pass);}
static unsigned pass=1,maxPass=4;
static bool restored=false,completed=false;
extern "C" void test_complete_app(unsigned ns){
 if(completed){finishStorage();return;}if(mode=="first-resume"||mode=="no-entropy"){assert(L::checkpoint(true));settle();assert(core(1)->identity().known==(mode=="first-resume"));assert(!core(6)->identity().known);if(mode=="first-resume"){assert(!core(1)->exportPrepared());C::Identity expected{};std::ifstream saved(output+"/../first-cut/first-target.bin",std::ios::binary);saved.read((char*)&expected,sizeof(expected));assert(saved.good()&&expected.known&&core(1)->identity().generation==expected.generation&&!memcmp(core(1)->identity().lineage,expected.lineage,16)&&!memcmp(core(1)->identity().digest,expected.digest,32));}assert(entropyFills==1&&!entropyWindow);finishStorage();return;}const auto*api=risc_runtime_get_api(1);assert(api&&L::ready());
 if(mode=="absent"||mode=="ambiguous"){
  assert(availability(ns)!=RISC_KEY_VALUE_AVAILABLE&&!core(ns)->ready());risc_runtime_capability_v1 g{sizeof(g)};assert(api->acquire(RISC_KEY_VALUE_CAPABILITY,1,ns,&g));auto*e=static_cast<const risc_key_value_v1*>(g.api);char data[8];uint32_t n=0;assert(e->get(e->context,"ui.theme",data,sizeof(data),&n)==RISC_KEY_VALUE_NOT_FOUND);assert(e->put(e->context,"ui.theme","default",7)!=RISC_KEY_VALUE_OK);bool sawPinned=false;unsigned loops=0;do{api->yield_ms(1);sawPinned|=L::discovering()&&port->nativeStorageAttached();assert(++loops<100000);}while(L::discovering());assert(sawPinned&&L::exitSafe()&&!core(1)->ready()&&!core(6)->ready());assert(availability(ns)==(mode=="ambiguous"?RISC_KEY_VALUE_CONFLICT:RISC_KEY_VALUE_UNAVAILABLE));assert(e->get(e->context,"ui.theme",data,sizeof(data),&n)==RISC_KEY_VALUE_NOT_FOUND);assert(api->release(&g));finishStorage();return;
 }
 if(mode=="async-restore"&&ns==1){
  const auto beforeNvs=nvs;const auto writes=f->counter(6);assert(availability(1)==RISC_KEY_VALUE_UNAVAILABLE&&!core(1)->ready());
  risc_runtime_capability_v1 data{sizeof(data)},atomic{sizeof(atomic)},status{sizeof(status)};
  assert(api->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&data)&&api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&atomic)&&api->acquire(RISC_KEY_VALUE_AVAILABILITY_CAPABILITY,1,1,&status));
  const auto&kv=*static_cast<const risc_key_value_v1*>(data.api);const auto&writer=*static_cast<const risc_key_value_atomic_v1*>(atomic.api);const auto&state=*static_cast<const risc_key_value_availability_v1*>(status.api);
  auto observed=[&](){risc_key_value_availability_state_v1 v{sizeof(v)};assert(!state.snapshot(state.context,&v));return v.availability;};
  char value[16]="draft-kept";uint32_t size=9;assert(kv.get(kv.context,"ui.theme",value,sizeof(value),&size)==RISC_KEY_VALUE_NOT_FOUND&&!size&&!strcmp(value,"draft-kept"));assert(kv.put(kv.context,"ui.theme","default",7)!=RISC_KEY_VALUE_OK&&nvs==beforeNvs);
  bool sawPinned=false;unsigned loops=0;do{auto polls=L::timing().lastPolls;api->yield_ms(1);assert(L::timing().lastPolls<=polls+1||!polls);if(L::discovering()&&port->nativeStorageAttached()){sawPinned=true;assert(port->nativeDiscoveryAppExitSafe()&&port->nativeDiscoveryProviderSafe()&&!port->appExitSafe()&&!port->providerStorageSafe());}assert(++loops<100000);}while(L::discovering());
  assert(sawPinned&&L::exitSafe()&&core(1)->ready()&&core(6)->ready()&&observed()==RISC_KEY_VALUE_RECONCILIATION_PENDING&&availability(6)==RISC_KEY_VALUE_AVAILABLE&&f->counter(6)==writes);
  assert(kv.get(kv.context,"ui.theme",value,sizeof(value),&size)==RISC_KEY_VALUE_NOT_FOUND&&!strcmp(value,"draft-kept"));risc_key_value_atomic_state_v1 captured{sizeof(captured)};assert(writer.state(writer.context,&captured)==RISC_ATOMIC_BUSY);
  assert(api->release(&atomic)&&observed()==RISC_KEY_VALUE_RECONCILIATION_PENDING);assert(api->release(&data)&&observed()==RISC_KEY_VALUE_AVAILABLE);assert(api->release(&status));verify(1,4);verify(6,4);puts("ASYNC restore: actual app remains usable; source authority waits for both retained data grants; no SD/default writes");
 }
 if(mode=="writer-identities"){
  static unsigned at=0;const unsigned namespaces[]={1,6,1,1,1,1};const char*next[]={"wifi.elf","home.elf","battery.elf","scanner.elf","contexts.elf"};
  assert(at<6&&ns==namespaces[at]&&availability(ns)==RISC_KEY_VALUE_AVAILABLE);risc_runtime_capability_v1 g{sizeof(g)};
  assert(!api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,ns==1?6:1,&g)&&!g.api);
  assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,ns,&g));auto&e=*static_cast<const risc_key_value_atomic_v1*>(g.api);risc_key_value_atomic_state_v1 captured{sizeof(captured)};
  const auto initialState=e.state(e.context,&captured);printf("WRITER state before maintenance collection id=%u namespace=%u result=%d\n",at,ns,initialState);fflush(stdout);
  assert(initialState==RISC_ATOMIC_OK||initialState==RISC_ATOMIC_BUSY);settle();
  assert(e.state(e.context,&captured)==RISC_ATOMIC_OK&&captured.namespace_id==ns);assert(api->release(&g));settle();
  printf("WRITER exact selected ID %u namespace=%u admitted; wrong namespace refused\n",at,ns);
  if(at<5)assert(api->request_launch(next[at]));else completed=true;++at;return;
 }
 assert(availability(ns)==RISC_KEY_VALUE_AVAILABLE&&core(ns)->ready());if(restored)verify(ns,(mode=="publication-resume"||mode=="publication-erase")&&ns==1?5:4);
 risc_runtime_capability_v1 g{sizeof(g)};assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,ns,&g));auto&e=*static_cast<const risc_key_value_atomic_v1*>(g.api);settle();edit(e,ns,pass);if(mode=="interrupt"&&ns==1){assert(core(1)->pendingCount()==1&&core(1)->identity().generation==5);Shim::stop();saveNative();saveMedia(cardPath.c_str());puts("POWER CUT after native receipt, before SD acknowledgement; previous SD generation preserved");fflush(stdout);std::_Exit(73);}if(mode=="publication-cut"&&ns==1){publicationCut=true;priorReplaces=f->counter(7);}assert(api->release(&g));settle();
 assert(L::exitSafe()&&!core(ns)->pendingCount()&&core(ns)->identity().known);
 printf("ACK namespace=%u pass=%u sd_generation=%u revision=%llu\n",ns,pass,unsigned(core(ns)->identity().generation),(unsigned long long)core(ns)->revision());
 if(ns==1)assert(api->request_launch("wifi.elf"));else if(pass<maxPass){++pass;assert(api->request_launch("settings.elf"));}else completed=true;
}
static void file(const char*p,const char*bytes){const auto*v=f->volume;auto*ext=risc_storage_volume_extension(v);auto h=v->file_open_write(v->context,p);assert(h&&v->file_write(v->context,h,bytes,strlen(bytes))==strlen(bytes)&&ext->file_sync(v->context,h)&&v->file_close(v->context,h,true));}
static void ordinaryComplete(){const auto*v=f->volume;uint64_t size=0;char out[16]{};auto h=v->file_open_read(v->context,"/ordinary.txt",&size);assert(h&&size==4&&v->file_read(v->context,h,out,4)==4&&!memcmp(out,"keep",4)&&v->file_close(v->context,h,true));auto*e=risc_storage_volume_export(v);uint64_t t=0,n=0;uint32_t s=0;assert(!e->export_begin(v->context,&t,&n,&s)&&s==512&&!e->export_end(v->context,t));}
static void saveNative(){assert(entropyFills==1&&!entropyWindow);printf("ENTROPY startup_attempts=%u captured=%d\n",entropyFills,!entropyFail);printf("MEMORY payload_peak=%zu internal_control_heap_peak=%zu payload_live=%zu internal_control_heap_live=%zu\n",Shim::peakPayload,Shim::peakInternal,Shim::livePayload,Shim::liveInternal);Bytes bytes(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(bytes.data(),bytes.size())==bytes.size());std::ofstream flash(output+"/native-flash.img",std::ios::binary);flash.write((char*)bytes.data(),bytes.size());assert(flash.good());std::ofstream binary(output+"/native-nvs.bin",std::ios::binary);auto word=[&](uint32_t n){binary.write((char*)&n,sizeof(n));};word(nvs.size());for(auto&space:nvs){word(space.first.size());binary.write(space.first.data(),space.first.size());word(space.second.size());for(auto&v:space.second){word(v.first.size());binary.write(v.first.data(),v.first.size());word(v.second.size());binary.write((char*)v.second.data(),v.second.size());}}binary.close();assert(binary.good());std::ofstream nv(output+"/native-nvs.txt");for(auto&s:nvs)for(auto&v:s.second){nv<<s.first<<'/'<<v.first<<' ';for(auto b:v.second)nv<<unsigned(b)<<',';nv<<'\n';}assert(nv.good());}
static void loadNative(const std::string&path){std::ifstream image(path+"/native-flash.img",std::ios::binary);Bytes bytes{std::istreambuf_iterator<char>(image),{}};assert(bytes.size()==native_lfs_fixture_flash_size()&&!native_lfs_fixture_restore(bytes.data(),bytes.size()));std::ifstream binary(path+"/native-nvs.bin",std::ios::binary);auto word=[&](){uint32_t n=0;binary.read((char*)&n,sizeof(n));assert(binary.good()&&n<100000);return n;};auto string=[&](){auto n=word();std::string v(n,' ');binary.read(v.data(),n);return v;};auto spaces=word();for(unsigned i=0;i<spaces;++i){auto space=string();auto count=word();for(unsigned j=0;j<count;++j){auto key=string();auto n=word();auto&v=nvs[space][key];v.resize(n);binary.read((char*)v.data(),n);}}assert(binary.good());}
static void finishStorage(){assert(L::shutdown());assert(!storageFinished&&W::exitSafe()&&L::exitSafe()&&!M::retained()&&handles.empty()&&iterators.empty());Shim::stop();saveNative();if(mode!="absent")ordinaryComplete();saveMedia(cardPath.c_str());assert(!anchorPin->release()&&!f);storageFinished=true;}
extern "C" size_t complete_host_runtime_size();extern "C" size_t complete_host_port_size();
int main(int argc,char**argv){assert(argc==6);mode=argv[2];output=argv[4];cardPath=argv[3];assert(complete_host_runtime_size()==sizeof(RiscBoot::Runtime)&&complete_host_port_size()==sizeof(Port));printf("LAYOUT Runtime=%zu CpuPort=%zu; paired-bank cohort on both host and ESP compilation boundaries\n",sizeof(RiscBoot::Runtime),sizeof(Port));
 std::ifstream initial(argv[5],std::ios::binary);Bytes initialBytes{std::istreambuf_iterator<char>(initial),{}};assert(initialBytes.size()==native_lfs_fixture_flash_size()&&!native_lfs_fixture_restore(initialBytes.data(),initialBytes.size()));if(mode=="first-resume")loadNative(output+"/../first-cut");if(mode=="publication-resume")loadNative(output+"/../publication-cut");native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();assert(M::prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition())));assert(!native_lfs_fixture_reboot()&&M::finish());entropyFail=mode=="first-resume"||mode=="no-entropy";assert(RiscCpu::NativeSettingsStartupEntropy::capture(owner)==!entropyFail);assert(B::prepare(owner));admission=B::admission();assert(admission&&F::maintenance());
 assert(L::prepare(admission,{owner,schedule,[](){return uint32_t(nowMs);},nullptr}));
 Port cpu(hardware());port=&cpu;RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;},bind};
 boot.appExitSafe=[](){return L::continuing()?port->nativeDiscoveryAppExitSafe():port->appExitSafe()&&L::exitSafe();};boot.providerStorageSafe=[](){return !L::retained()&&(L::continuing()?port->nativeDiscoveryProviderSafe():port->providerStorageSafe());};boot.retainedDelay=schedule;boot.nativeOwnerCheckpoint=[](bool admit){return L::checkpoint(admit);};boot.nativeDiscoveryCheckpoint=[](){return L::discoveryCheckpoint();};boot.nativeSettingsShutdown=[](){return L::shutdown();};
 static const RiscBoot::KeyValueBackend legacy{nullptr,[](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},[](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},2048};boot.keyValue=&legacy;
 RiscBoot::Runtime runtime(boot);assert(L::bind(runtime));if(!runtime.prepare(argv[1])){fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}Pin anchor(runtime,cpu,9);anchorPin=&anchor;assert(!anchor.acquire());assert(f&&loadMedia&&saveMedia);
 const bool existing=std::filesystem::exists(argv[3]);if(existing)loadMedia(argv[3]);
 if(mode=="absent")f->fault(8);else{P fresh(runtime,cpu,1,9);bootstrap(fresh);assert(!fresh.release());if(!existing)file("/ordinary.txt","keep");}
 if(mode=="ambiguous"){auto*e=risc_storage_volume_extension(f->volume);assert(e->mkdir(f->volume->context,"/RTECFG")&&e->mkdir(f->volume->context,"/RTECFG/00000001"));file("/RTECFG/00000001/replica.index","invalid-ambiguous");assert(e->mkdir(f->volume->context,"/RTECFG/00000006"));file("/RTECFG/00000006/replica.index","invalid-ambiguous");}
 if(mode=="conflict")nvs["rte00000001"]["valuable"]={'k','e','e','p'};
 const auto beforeNvs=nvs;const auto writes=f->counter(6);
 assert(L::hydrate(runtime,cpu)&&L::ready()&&L::attachOwner(runtime,cpu));assert(((mode=="first-resume"||mode=="publication-resume")||( !core(1)->ready()&&!core(6)->ready()))&&f->counter(6)==writes);
 if(mode!="absent"&&mode!="ambiguous"&&mode!="async-restore")discovery();firstCut=mode=="first-cut";
 if(mode=="conflict"){assert(!core(1)->ready()&&availability(1)!=RISC_KEY_VALUE_AVAILABLE&&core(6)->ready()&&nvs.at("rte00000001")==beforeNvs.at("rte00000001")&&f->counter(6)==writes);}
 else if(mode=="absent"||mode=="ambiguous"){assert(!core(1)->ready()&&nvs==beforeNvs&&f->counter(6)==writes);assert(runtime.run()&&L::exitSafe()&&!runtime.retained()&&nvs==beforeNvs);}
 else{assert(mode=="async-restore"||(core(1)->ready()&&core(6)->ready()));if(existing&&mode!="first-resume"){restored=true;const bool published=mode=="publication-resume"||mode=="publication-erase";pass=maxPass=published?6:5;if(mode!="async-restore"){verify(1,published?5:4);verify(6,4);}assert(f->counter(6)==writes);}bool ran=runtime.run();fprintf(stderr,"run=%d error=%s retained=%d lifecycle=%d completed=%d\n",ran,runtime.error(),runtime.retained(),L::exitSafe(),completed);assert(ran&&!runtime.retained()&&L::exitSafe());if(mode!="first-resume"&&mode!="no-entropy"&&mode!="writer-identities"){verify(1,maxPass);verify(6,maxPass);for(auto ns:{1u,6u})assert(core(ns)->identity().generation>=maxPass&&!core(ns)->pendingCount());}}
 if(!storageFinished)finishStorage();assert(L::shutdown());printf("PASS complete concrete settings path %s; native NVS calls=%u; full block-native fixture and real SD worker\n",mode.c_str(),nvsCalls);fflush(stdout);std::_Exit(0);
}
