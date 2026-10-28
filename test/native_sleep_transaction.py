from pathlib import Path
import subprocess,json,hashlib,argparse,os
parser=argparse.ArgumentParser(description='Actual Runtime, native serial, SD/FatFs and CPU sleep transaction host qualification')
parser.add_argument('--product',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--sanitize',action='store_true')
options=parser.parse_args()
R=Path(__file__).resolve().parents[1];P=options.product.resolve();O=options.output.resolve();O.mkdir(parents=True,exist_ok=True)
# Test-only copies; every production source stays unchanged.
orig=(R/'test/native_serial_waveshare_sd_test.cpp').read_text()
orig=orig.replace('#include "bootstrap/Runtime.h"','#define private public\n#include "bootstrap/Runtime.h"\n#undef private\n#define private public\n#include "ports/esp32s3/CpuPort.h"\n#undef private\n#include "ports/esp32s3/NativeSdmmc.h"')
start=orig.index('extern "C" void test_atomic_app('); end=orig.index('\nint main(',start)
custom=r'''
namespace NS=RiscCpu::NativeSdmmc;
static uint64_t nativeSectors;
static unsigned nestedFences;
static bool nativeOwned(){return true;}
void* heap_caps_malloc(size_t n,unsigned){return std::malloc(n);}
void heap_caps_free(void* p){std::free(p);}
esp_err_t gpio_reset_pin(gpio_num_t pin){assert(pin==1||pin==2||pin==3);return ESP_OK;}
esp_err_t sdmmc_host_init(){return ESP_OK;}
esp_err_t sdmmc_host_deinit(){return ESP_OK;}
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t*c){assert(slot==1&&c->clk==2&&c->cmd==1&&c->d0==3&&c->width==1);return ESP_OK;}
esp_err_t sdmmc_card_init(const sdmmc_host_t*,sdmmc_card_t*c){c->csd={512,uint32_t(nativeSectors)};c->max_freq_khz=20000;return ESP_OK;}
esp_err_t sdmmc_read_sectors(sdmmc_card_t*,void*,size_t,size_t){assert(false);return ESP_FAIL;}
esp_err_t sdmmc_write_sectors(sdmmc_card_t*,const void*,size_t,size_t){assert(false);return ESP_FAIL;}
esp_err_t sdmmc_get_status(sdmmc_card_t*){return ESP_OK;}
extern "C" bool wave_native_sd_open(risc_sdmmc_card_info_v1* i){
 nativeSectors=i->sector_count;NS::configure(nativeOwned);return NS::open(2,1,3,RISC_SDMMC_MAX_HZ,i);
}
extern "C" bool wave_native_sd_close(){
 if(runtime && app && runtime->sleepCalling_){
  assert(!runtime->active()&&!risc_runtime_get_api(1)&&!runtime->residentResetSafe());
  uint64_t token=99;assert(app->sleep_prepare(&token)==RISC_RUNTIME_SLEEP_BUSY&&!token);
  assert(app->sleep_resume(runtime->sleepToken_)==RISC_RUNTIME_SLEEP_CONTEXT);++nestedFences;
 }
 return NS::closed()||NS::close();
}
namespace NativeStorageWorker {bool idle(){return true;}}
namespace NativeSdmmc {bool closed(){return NS::closed();}}
namespace NativeSleep {bool stackReady(){return true;}}
namespace NativeI2s {bool idle(){return true;}}
namespace NativeRadioAsync {bool idle(){return true;}}
namespace NativeHci {bool idle(){return true;}}
// This serial/SD fixture composes the real private LittleFS mount, but not an
// AppData writer task. Model its independent worker guard explicitly; retain
// the real mount-held/retained barrier instead of an unconditional success stub.
namespace RiscAppDataMount {bool held();bool retained();}
static bool appDataPending,appDataRetained;
namespace RiscAppData {bool exitSafe(){return !appDataPending&&!appDataRetained&&
 !RiscAppDataMount::held()&&!RiscAppDataMount::retained();}}
struct {bool held=false,pending=false;} spi[1];
#include "deep_ready_exact.inc"
struct Terminal {};
extern "C" void test_atomic_app(unsigned hook,unsigned){
 ++appCalls;app=risc_runtime_get_api(1);assert(app);
 if(hook!=1){assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_CONTEXT);return;}
 assert(S::sdFiles().bound()&&counter(3)==1);
 assert(N::exitSafe()&&N::custodySafe()&&S::sdFiles().exitSafe());
 assert(!counter(6)&&!counter(11));
 const bool mounted=mode=="healthy"||mode=="abandoned";
 assert(bool(counter(7))==mounted);
 assert(NS::idle()&&NS::closed()!=mounted);
 RiscCpu::Hardware h{};h.owner=owner;h.deepReady=deepReady;
 h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};h.gpioClose=[](uint8_t){return true;};
 h.gpioRead=[](uint8_t pin,bool*out){assert(pin==0);*out=true;return true;};
 h.deepWakeValid=[](uint8_t pin){return pin==0;};
 h.deepWakeArm=[](uint8_t,bool,bool){return true;};h.deepWakeClear=[](uint8_t,bool){return true;};
 h.deepSleep=[](){throw Terminal{};};
 RiscCpu::Port cpu(h);cpu.runtime_=runtime;auto& g=cpu.gpios_[0];g.port=&cpu;g.input=1;g.pullup=1;
 uint64_t key=0;assert(RiscCpu::Port::gpioClaim(&g,0,false,false,true,&key));
 assert(!cpu.nativeStoragePinned()&&!cpu.storageLifecycleBusy());
 assert(runtime->residentResetSafe());

 uint64_t sleepToken=99;
 assert(app->sleep_prepare(nullptr)==RISC_RUNTIME_SLEEP_CONTEXT);
 const unsigned operations=counter(0);
 owned=false;assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_CONTEXT&&!sleepToken);owned=true;
 platformSafe=false;assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_BUSY&&!sleepToken);platformSafe=true;
 runtime->invocation_->queued_[0]='x';assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_BUSY&&!sleepToken);runtime->invocation_->queued_[0]=0;
 runtime->shellLoading_=true;assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_BUSY&&!sleepToken);runtime->shellLoading_=false;
 assert(counter(0)==operations);
 if(mounted){
  uint32_t file=0;assert(S::sdFiles().open("/unrelated.txt",RISC_FILES_READ,&file)==RISC_FILES_OK&&file);
  const unsigned before=counter(0);assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_BUSY&&!sleepToken&&counter(0)==before);
  assert(S::sdFiles().close(file)==RISC_FILES_OK);
 }
 if(mode=="abandoned"){
  assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_OK&&sleepToken&&NS::closed());return;
 }

 // A resident foreground may enter a reversible sleep transaction, but the
 // unchanged CPU Deep gate must still refuse losing its suspended stack.
 runtime->residentEnabled_=runtime->foregroundRunning_=true;
 assert(!runtime->residentResetSafe());
 assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_OK&&sleepToken&&NS::closed());
 assert(!runtime->residentResetSafe());
 assert(RiscCpu::Port::gpioDeepSleep(&g,key,false)==RISC_DEEP_SLEEP_BUSY);
 assert(app->sleep_resume(sleepToken)==(mode=="absent-card"?RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE:RISC_RUNTIME_SLEEP_OK));
 runtime->residentEnabled_=runtime->foregroundRunning_=false;
 assert(runtime->residentResetSafe());

 assert(app->sleep_prepare && app->sleep_resume);
 for(unsigned cycle=0;cycle<2;++cycle){
  int sr=app->sleep_prepare(&sleepToken);
  printf("prepare mode=%s cycle=%u rc=%d token=%llu\n",mode.c_str(),cycle,sr,(unsigned long long)sleepToken);fflush(stdout);
  assert(sr==RISC_RUNTIME_SLEEP_OK&&sleepToken&&NS::closed());
  assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY);
  uint32_t file=99;assert(S::sdFiles().open("/unrelated.txt",RISC_FILES_READ,&file)==RISC_FILES_BUSY&&!file);
  assert(app->sleep_resume(sleepToken+1)==RISC_RUNTIME_SLEEP_CONTEXT);
  int rr=app->sleep_resume(sleepToken);
  assert(rr==(mode=="absent-card"?RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE:RISC_RUNTIME_SLEEP_OK));
  assert(app->sleep_resume(sleepToken)==RISC_RUNTIME_SLEEP_CONTEXT);
 }
 assert(app->sleep_prepare(&sleepToken)==RISC_RUNTIME_SLEEP_OK&&sleepToken&&NS::closed());
 int rc=0;bool entered=false;
 try{rc=RiscCpu::Port::gpioDeepSleep(&g,key,false);}catch(const Terminal&){entered=true;}
 printf("REPRO mode=%s provider_open=%u serial_exit_safe=%u native_idle=%u native_closed=%u native_custody_pin=%u deepReady=%u result=%d terminal=%u\n",mode.c_str(),counter(7),N::exitSafe(),NS::idle(),NS::closed(),cpu.nativeStoragePinned(),deepReady(),rc,entered);
 fflush(stdout);
 assert(entered&&nestedFences); // All idle native volume states now reach terminal Deep.
 puts("Owner-safe mounted SD prepare/resume and terminal Deep PASS");fflush(stdout);
 _exit(0); // Modeled terminal Deep never returns to app, graph, or cleanup.

}
'''
# mark unused helper functions in existing harness; tests replaced, no production mutation
orig=orig[:start]+custom+orig[end:]
orig=orig.replace('const bool result=runtime->run();',r'''const bool result=runtime->run();
    if(mode=="abandoned"){
        assert(!result&&runtime->retained()&&runtime->sleepToken_&&NS::closed());
        assert(counter(3)==1&&counter(5)==0&&S::sdFiles().bound()&&S::sdFiles().retained());
        assert(runtime->nativeFiles_[0].grant.slot&&runtime->nativeFiles_[0].sleepFrozen);
        const unsigned operations=counter(0);uint32_t file=99;
        // Returning with an abandoned transaction is terminal custody, not a
        // reversible in-app sleep fence. The still-frozen grant stays pinned.
        assert(S::sdFiles().open("/unrelated.txt",RISC_FILES_READ,&file)==RISC_FILES_RETAINED&&!file);
        assert(!runtime->nativeFilesCheckpoint()&&!runtime->stopNativeFiles());
        assert(counter(0)==operations&&counter(3)==1&&!counter(4)&&!counter(5));
        puts("Abandoned transaction retains invocation, graph and frozen native dispatch PASS");fflush(stdout);_exit(0);
    }''')
(O/'serial_sd_deep_repro.cpp').write_text(orig)
fixture=(R/'test/native_serial_waveshare_sd_fixture.c').read_text()
fixture=fixture.replace('static void owner_io(void)', 'extern bool wave_native_sd_open(risc_sdmmc_card_info_v1*);\nextern bool wave_native_sd_close(void);\nstatic void owner_io(void)')
fixture=fixture.replace('return work_open(c,i);','return work_open(c,i) && wave_native_sd_open(i);')
fixture=fixture.replace('return work_close(c);','return wave_native_sd_close() && work_close(c);')
(O/'serial_sd_deep_fixture.c').write_text(fixture)
hardware=(R/'src/ports/esp32s3/NativeHardware.cpp').read_text(); a=hardware.index('bool deepReady(){'); b=hardware.index('\nbool wakeValid(',a)
(O/'deep_ready_exact.inc').write_text(hardware[a:b]+'\n')
sdk=O/'sdk';sdk.mkdir(exist_ok=True)
for base in (R/'test/fixtures/watch/sdk/driver',P/'include',R/'sdk/driver',R/'sdk/app',R/'sdk/hardware'):
 for file in base.glob('*.h'):(sdk/file.name).write_bytes(file.read_bytes())
(O/'sd.json').write_bytes((P/'drivers/storage_sdmmc/manifest-async.json').read_bytes())
board=json.loads((P/'hardware/board.json').read_text());board['buses']=[];board['devices']=[d for d in board['devices'] if d['instance_id']==12]
(O/'board.json').write_text(json.dumps(board))
(O/'app.json').write_text(json.dumps(dict(type='application',id='serial-wave-test',version='1.0.0',architecture='xtensa-esp32s3',file_name='default.elf',entry='app_main',requires=[])))
(O/'boot.json').write_text(json.dumps(dict(board='board.json',default_app='default.elf',provider_activation='demand',drivers=[dict(manifest='sd.json',instance_id=12)],app_capabilities=[dict(manifest='app.json',grants=[])])))
flags=['-O0','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-misleading-indentation']
if options.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
incs=['-I'+str(x) for x in (O,sdk,R,R/'src',R/'test',R/'test/native_sdmmc_shim',R/'test/drivers/stubs',R/'test/native_sleep_diagnostics_idf6_shim',R/'test/native_platform_shim',R/'lib/ArduinoJson/src')]
cmds=[]
def run(args):
 args=list(map(str,args));cmds.append(args);subprocess.run(args,check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
run(['cc','-std=c11',*flags,*incs,'-shared','-fPIC','-fvisibility=hidden',R/'test/fixtures/atomic_key_value_app.c','-o',O/'default.elf'])
run(['cc','-std=c11',*flags,*incs,'-shared','-fPIC','-fvisibility=hidden','-DWAVE_SERIAL_TEST_SOURCE="'+str(P/'tests/test_storage_async.c')+'"',O/'serial_sd_deep_fixture.c',P/'lib/storage_fatfs/fatfs/ff.c',P/'lib/storage_fatfs/fatfs/ffunicode.c','-o',O/'driver.elf'])
sources=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/runtime/serial/Files.cpp','src/ports/esp32s3/NativeSerialFiles.cpp','src/ports/esp32s3/CpuPort.cpp']
# The shared serial fixture owns real independent private namespace 2. Reuse
# its pinned LittleFS composition instead of supplying no-op lifecycle symbols.
import compile_native_serial_littlefs
lfs_objects,lfs_selection,lfs_link=compile_native_serial_littlefs.build(R,O/'littlefs-composition',run,'address,undefined' if options.sanitize else 'none')
run(['c++','-std=c++17',*flags,*incs,*lfs_selection,'-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_SERIAL_SD=1','-DRISC_NATIVE_ESP_IDF=1','-DRISC_ENABLE_SDMMC=1','-DRISC_NATIVE_STORAGE_CUSTODY=1','-rdynamic','-no-pie','-pthread',*[R/x for x in sources],O/'serial_sd_deep_repro.cpp',*lfs_objects,*lfs_link,'-ldl','-o',O/'test'])
(O/'commands.json').write_text(json.dumps(cmds,indent=2))
for mode in ('cold','absent-card','healthy','abandoned'):run([O/'test',O,mode])
(O/'receipt.json').write_text(json.dumps({'hardware_tested':False,'modeled':['AppData worker pending/retained guard; real shared LittleFS mount barrier; standalone worker qualification is separate'],'sanitized':options.sanitize,'leak_detection':False,'modes':['cold','absent-card','healthy','abandoned'],'commands':cmds,'generated_source_sha256':{str(f.relative_to(O)):hashlib.sha256(f.read_bytes()).hexdigest() for f in O.rglob('*') if f.is_file() and f.suffix in ('.c','.cpp','.h','.inc')},'source_sha256':{str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*[R/x for x in sources],R/'src/bootstrap/RuntimeSleep.inc',R/'test/native_serial_waveshare_sd_test.cpp',R/'test/compile_native_serial_littlefs.py',R/'test/native_serial_littlefs_composition.cpp',R/'src/ports/esp32s3/NativeSerialStorage.cpp',R/'src/ports/esp32s3/NativeAppDataMount.cpp',R/'src/runtime/storage/ReopenedFiles.cpp',Path(__file__)]}},indent=2)+'\n')
