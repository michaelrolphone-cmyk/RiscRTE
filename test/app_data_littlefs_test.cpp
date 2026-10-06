/* Actual pinned LittleFS core with the ESP adapter's geometry, on a NOR model.
 * Models torn/before/after program+erase cuts; never accesses hardware. */
#include "lfs.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
struct Disk {
 std::vector<uint8_t> bytes;
 int fail=-1,count=0,mode=0;
 bool dead=false;
};
static int readBlock(const lfs_config*c,lfs_block_t block,lfs_off_t off,void*out,lfs_size_t size){
 auto&d=*static_cast<Disk*>(c->context);if(d.dead)return LFS_ERR_IO;
 size_t at=size_t(block)*4096+off;if(off+size>4096 || at+size>d.bytes.size())return LFS_ERR_IO;
 memcpy(out,d.bytes.data()+at,size);return 0;
}
static int program(const lfs_config*c,lfs_block_t block,lfs_off_t off,const void*bytes,lfs_size_t size){
 auto&d=*static_cast<Disk*>(c->context);if(d.dead)return LFS_ERR_IO;
 size_t at=size_t(block)*4096+off;if(off+size>4096 || at+size>d.bytes.size())return LFS_ERR_IO;
 bool cut=++d.count==d.fail;size_t amount=cut?(d.mode==0?0:d.mode==1?size/2:size):size;
 for(size_t i=0;i<amount;++i){uint8_t byte=static_cast<const uint8_t*>(bytes)[i];assert((d.bytes[at+i]&byte)==byte);d.bytes[at+i]&=byte;}
 if(cut){d.dead=true;return LFS_ERR_IO;}return 0;
}
static int erase(const lfs_config*c,lfs_block_t block){
 auto&d=*static_cast<Disk*>(c->context);if(d.dead)return LFS_ERR_IO;
 size_t at=size_t(block)*4096;if(at+4096>d.bytes.size())return LFS_ERR_IO;
 bool cut=++d.count==d.fail;size_t amount=cut?(d.mode==0?0:d.mode==1?2048:4096):4096;
 std::fill(d.bytes.begin()+at,d.bytes.begin()+at+amount,0xff);
 if(cut){d.dead=true;return LFS_ERR_IO;}return 0;
}
static int sync(const lfs_config*c){return static_cast<Disk*>(c->context)->dead?LFS_ERR_IO:0;}
struct Fs {
 Disk&disk;lfs_t lfs{};lfs_config cfg{};
 uint8_t readCache[512]{},programCache[512]{},lookahead[128]{};
 explicit Fs(Disk&d):disk(d){cfg.context=&d;cfg.read=readBlock;cfg.prog=program;cfg.erase=erase;cfg.sync=sync;cfg.read_size=128;cfg.prog_size=128;cfg.block_size=4096;cfg.block_count=0;cfg.cache_size=512;cfg.lookahead_size=128;cfg.block_cycles=512;cfg.read_buffer=readCache;cfg.prog_buffer=programCache;cfg.lookahead_buffer=lookahead;}
 int mount(){return lfs_mount(&lfs,&cfg);}
 void unmount(){assert(lfs_unmount(&lfs)==0);}
};
static int writeFile(Fs&fs,const char*path,const std::string&bytes){
 lfs_file_t file{};lfs_file_config config{};uint8_t cache[512]{};config.buffer=cache;
 int result=lfs_file_opencfg(&fs.lfs,&file,path,LFS_O_WRONLY|LFS_O_CREAT|LFS_O_EXCL,&config);if(result)return result;
 for(size_t at=0;at<bytes.size();at+=512){size_t n=std::min(size_t(512),bytes.size()-at);int wrote=lfs_file_write(&fs.lfs,&file,bytes.data()+at,n);if(wrote!=int(n)){lfs_file_close(&fs.lfs,&file);return LFS_ERR_IO;}}
 result=lfs_file_sync(&fs.lfs,&file);int closed=lfs_file_close(&fs.lfs,&file);return result?result:closed;
}
static std::string readFile(Fs&fs,const char*path){
 lfs_file_t file{};lfs_file_config config{};uint8_t cache[512]{};config.buffer=cache;assert(lfs_file_opencfg(&fs.lfs,&file,path,LFS_O_RDONLY,&config)==0);
 lfs_soff_t size=lfs_file_size(&fs.lfs,&file);assert(size>=0 && size<=65536);std::string bytes(size,'\0');assert(lfs_file_read(&fs.lfs,&file,bytes.data(),bytes.size())==size);assert(lfs_file_close(&fs.lfs,&file)==0);return bytes;
}
static int replace(Fs&fs,const std::string&bytes){
 int result=writeFile(fs,"/n00000001/.pending",bytes);if(result)return result;
 // Exact production ordering: sync+close, verify stage, one rename, verify new.
 if(readFile(fs,"/n00000001/.pending")!=bytes)return LFS_ERR_IO;
 result=lfs_rename(&fs.lfs,"/n00000001/.pending","/n00000001/timecard.json");
 if(result)return result;
 return readFile(fs,"/n00000001/timecard.json")==bytes?0:LFS_ERR_IO;
}
int main(int argc,char**argv){
 assert(argc==2 || argc==3);std::ifstream input(argv[1],std::ios::binary);Disk seed{{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()}};assert(seed.bytes.size()==524288);
 Fs initial(seed);assert(initial.mount()==0);lfs_fsinfo info{};assert(lfs_fs_stat(&initial.lfs,&info)==0 && info.block_size==4096 && info.block_count==128);std::cout<<"Mounted initial disk format 0x"<<std::hex<<info.disk_version<<std::dec<<" using pinned LittleFS "<<LFS_VERSION_MAJOR<<"."<<LFS_VERSION_MINOR<<"\n";
 lfs_dir_t directory{};assert(lfs_dir_open(&initial.lfs,&directory,"/")==0);lfs_info entry{};unsigned entries=0;while(lfs_dir_read(&initial.lfs,&directory,&entry)>0){assert(!strcmp(entry.name,".") || !strcmp(entry.name,".."));++entries;}assert(entries==2);assert(lfs_dir_close(&initial.lfs,&directory)==0);
 assert(lfs_mkdir(&initial.lfs,"/n00000001")==0);const std::string old(49151,'o'),next(49151,'n');assert(writeFile(initial,"/n00000001/timecard.json",old)==0);initial.unmount();
 Disk good{seed.bytes};Fs goodFs(good);assert(goodFs.mount()==0);good.count=0;assert(replace(goodFs,next)==0);int operations=good.count;goodFs.unmount();
 for(int mode=0;mode<3;++mode)for(int cut=1;cut<=operations;++cut){
  Disk interrupted{seed.bytes};Fs running(interrupted);assert(running.mount()==0);interrupted.count=0;interrupted.fail=cut;interrupted.mode=mode;
  int result=replace(running,next);assert(result<0 && interrupted.dead);running.unmount();
  Disk rebooted{interrupted.bytes};Fs after(rebooted);assert(after.mount()==0);std::string actual=readFile(after,"/n00000001/timecard.json");assert(actual==old || actual==next);after.unmount();
 }
 // Exercise full logical quotas for two namespaces plus atomic staging space.
 Disk quotaDisk{seed.bytes};Fs quotaFs(quotaDisk);assert(quotaFs.mount()==0);
 assert(writeFile(quotaFs,"/n00000001/second",std::string(49151,'s'))==0);
 assert(writeFile(quotaFs,"/n00000001/third",std::string(32770,'t'))==0);
 assert(lfs_mkdir(&quotaFs.lfs,"/n00000002")==0);
 assert(writeFile(quotaFs,"/n00000002/a",std::string(65536,'a'))==0);
 assert(writeFile(quotaFs,"/n00000002/b",std::string(65536,'b'))==0);
 for(unsigned i=0;i<32;++i){std::string changed(49151,char('A'+i%26));assert(replace(quotaFs,changed)==0);}
 bool full=false;std::string pressureFolder;
 for(unsigned ns=3;ns<=8 && !full;++ns){
  std::string folder="/n0000000"+std::to_string(ns);pressureFolder=folder;assert(lfs_mkdir(&quotaFs.lfs,folder.c_str())==0);
  for(unsigned i=0;i<2 && !full;++i){std::string name=folder+"/"+std::to_string(i);int result=writeFile(quotaFs,name.c_str(),std::string(65536,'f'));if(result<0){full=true;assert(lfs_remove(&quotaFs.lfs,name.c_str())==0);}}
 }
 assert(full);
 // A failed 64KiB creation can leave enough room for a49KiB replacement after
 // its stage is reclaimed. Add at most32KiB within the same namespace quota.
 std::string tail=pressureFolder+"/tail";if(writeFile(quotaFs,tail.c_str(),std::string(32768,'p'))<0)assert(lfs_remove(&quotaFs.lfs,tail.c_str())==0);
 const std::string last(49151,char('A'+31%26));assert(readFile(quotaFs,"/n00000001/timecard.json")==last);
 assert(replace(quotaFs,std::string(49151,'z'))<0);assert(readFile(quotaFs,"/n00000001/timecard.json")==last);
 int cleanup=lfs_remove(&quotaFs.lfs,"/n00000001/.pending");assert(cleanup==0 || cleanup==LFS_ERR_NOENT);quotaFs.unmount();
 Disk fullReboot{quotaDisk.bytes};Fs fullAfter(fullReboot);assert(fullAfter.mount()==0 && readFile(fullAfter,"/n00000001/timecard.json")==last);fullAfter.unmount();
 std::cout<<"Bounded namespace quotas,32 full-size replacements and failed physical-full save/remount preserve old file PASS\n";
 // Read-only remount of the exact successful image remains complete.
 Disk verified{good.bytes};Fs finalFs(verified);assert(finalFs.mount()==0 && readFile(finalFs,"/n00000001/timecard.json")==next);lfs_fsinfo finalInfo{};assert(lfs_fs_stat(&finalFs.lfs,&finalInfo)==0);std::cout<<"Post-write disk format 0x"<<std::hex<<finalInfo.disk_version<<std::dec<<"\n";finalFs.unmount();
 if(argc==3){std::ofstream output(argv[2],std::ios::binary);output.write(reinterpret_cast<const char*>(good.bytes.data()),good.bytes.size());assert(output.good());}
 std::cout<<"Real LittleFS: max Timecard complete replacement; "<<operations*3<<" before/torn/after block-write/erase power cuts recover complete old/new files PASS\n";
}
