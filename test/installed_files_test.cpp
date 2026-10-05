#include "bootstrap/InstalledFiles.h"
#include <cassert>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
using RiscBoot::InstalledFiles;
static void put(const std::string&p,const std::string&s){std::ofstream f(p,std::ios::binary);f<<s;assert(f.good());}
int main(int argc,char**argv){assert(argc==2);std::string root=argv[1];assert(!mkdir((root+"/apps").c_str(),0700));
 put(root+"/apps/readme.json",std::string(1025,'A'));put(root+"/boot.json","private boot input");put(root+"/profile.json","secret must not appear");put(root+"/apps/readme.elf","ELF bytes");
 InstalledFiles::Name names[2]{};strcpy(names[0].value,"apps/readme.json");strcpy(names[1].value,"apps/readme.elf");InstalledFiles files;
 assert(files.configure(root.c_str(),names,2));assert(!files.configure(root.c_str(),names,2));
 uint64_t size=123;bool dir=false;for(const char*p:{"", "relative", "//apps", "/apps/", "/apps/../profile.json", "/apps/./readme.json", "/apps\\readme.json", "/profile.json", "/boot.json", "/apps/missing.json"})assert(!files.stat(p,&size,&dir));
 assert(files.stat("/apps",&size,&dir)&&dir);assert(files.stat("/apps/readme.json",&size,&dir)&&!dir&&size==1025);
 auto d=files.dirOpen("/");assert(d && !files.dirOpen("/"));risc_storage_dirent_v1 e{};assert(files.dirNext(d,&e) && !strcmp(e.name,"apps") && e.is_directory);assert(!files.dirNext(d,&e));char error[96];assert(files.error(error,sizeof(error))&&!error[0]);files.dirClose(d);assert(!files.dirNext(d,&e));
 d=files.dirOpen("/apps");assert(d);assert(files.dirNext(d,&e)&&!strcmp(e.name,"readme.elf"));assert(files.dirNext(d,&e)&&!strcmp(e.name,"readme.json"));assert(!files.dirNext(d,&e));files.dirClose(d);
 auto f=files.fileOpen("/apps/readme.json",&size);assert(f&&size==1025);unsigned char bytes[1200];memset(bytes,0xcd,sizeof(bytes));assert(files.fileRead(f,bytes,sizeof(bytes))==512);assert(bytes[511]=='A'&&bytes[512]==0xcd);assert(files.fileRead(f,bytes,sizeof(bytes))==512);assert(files.fileRead(f,bytes,sizeof(bytes))==1);assert(!files.fileRead(f,bytes,sizeof(bytes)));assert(files.fileClose(f)&&!files.fileClose(f));auto fresh=files.fileOpen("/apps/readme.json",&size);assert(fresh&&fresh!=f&&!files.fileRead(f,bytes,1));assert(files.fileClose(fresh));
 f=files.fileOpen("/apps/readme.json",&size);assert(f);put(root+"/replacement",std::string(1025,'B'));assert(!rename((root+"/replacement").c_str(),(root+"/apps/readme.json").c_str()));assert(!files.fileRead(f,bytes,10));assert(files.error(error,sizeof(error)) && strstr(error,"changed"));assert(files.fileClose(f));
 f=files.fileOpen("/apps/readme.json",&size);assert(f);put(root+"/apps/readme.json","short");assert(!files.fileRead(f,bytes,10));assert(files.fileClose(f));
 assert(!unlink((root+"/apps/readme.json").c_str()));assert(!symlink((root+"/profile.json").c_str(),(root+"/apps/readme.json").c_str()));assert(!files.fileOpen("/apps/readme.json",&size));assert(!files.stat("/apps/readme.json",&size,&dir));
 assert(!unlink((root+"/apps/readme.json").c_str()));put(root+"/apps/readme.json","restored");f=files.fileOpen("/apps/readme.json",&size);assert(f);files.end();assert(!files.fileRead(f,bytes,1));
 InstalledFiles invalid;InstalledFiles::Name bad[2]{};strcpy(bad[0].value,"apps/../private");assert(!invalid.configure(root.c_str(),bad,1));bad[0]=names[0];bad[1]=names[0];assert(!invalid.configure(root.c_str(),bad,2));
 puts("Installed files: exact inventory, traversal/alias/symlink refusal, virtual folders, bounded reads, replacement/in-place stale detection, EOF and handle retirement PASS");}
