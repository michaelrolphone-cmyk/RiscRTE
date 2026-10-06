#include "ports/esp32s3/NativeAppData.cpp"
#include <cassert>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
static esp_partition_t partition{ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x41),0x270000,0x80000,"appdata",false};
static bool absent=false,mountFail=false,ownerEnabled=true,safeEnabled=true;static unsigned mounts=0;
const esp_partition_t* esp_partition_find_first(esp_partition_type_t type,esp_partition_subtype_t subtype,const char*label){assert(type==ESP_PARTITION_TYPE_DATA && subtype==0x41 && !strcmp(label,"appdata"));return absent?nullptr:&partition;}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*c){++mounts;assert(!strcmp(c->base_path,"/appdata") && !strcmp(c->partition_label,"appdata") && !c->partition && !c->format_if_mount_failed && !c->grow_on_mount && !c->dont_mount && !c->read_only);return mountFail?-1:0;}
int64_t esp_timer_get_time(){return 1000;}
void vTaskDelay(unsigned){}
extern "C" int __wrap_stat(const char*path,struct stat*s){if(!strcmp(path,"/appdata")){*s={};s->st_mode=S_IFDIR;return 0;}errno=ENOENT;return -1;}
static bool owner(){return ownerEnabled;}static bool safe(){return safeEnabled;}
int main(int argc,char**argv){assert(argc==2);const char*mode=argv[1];
 if(!strcmp(mode,"missing"))absent=true;
 if(!strcmp(mode,"size"))partition.size-=4096;
 if(!strcmp(mode,"offset"))partition.address+=4096;
 if(!strcmp(mode,"encrypted"))partition.encrypted=true;
 if(!strcmp(mode,"mount"))mountFail=true;
 if(!strcmp(mode,"owner"))ownerEnabled=false;
 if(!strcmp(mode,"unsafe"))safeEnabled=false;
 bool expected=!strcmp(mode,"ok");assert(RiscAppData::prepare(owner,safe)==expected);
 assert(mounts==unsigned(expected || mountFail));assert(RiscAppData::exitSafe());
 auto*api=RiscAppData::backend();uint32_t n=99;uint64_t revision=99;
 assert(api->stat(api->context,1,"timecard.json",&n,&revision)==(expected?RISC_APP_DATA_NOT_FOUND:RISC_APP_DATA_UNAVAILABLE));assert(!n && !revision);
 assert(!RiscAppData::prepare(owner,safe));
}
