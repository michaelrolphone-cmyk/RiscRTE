// Offline owner input packaging. No network, device, NVS write or erase API.
#include "runtime/provisioning/Profile.h"
#include "runtime/provisioning/BootstrapInput.h"
#include "runtime/provisioning/TimeInput.h"
#include "runtime/provisioning/Installer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <array>
#include <cstring>
#include <unistd.h>
#include <vector>
namespace fs=std::filesystem;
static void write(const fs::path& path,const std::string& data){
 std::ofstream out(path,std::ios::binary);out.exceptions(std::ios::badbit|std::ios::failbit);
 out.write(data.data(),data.size());out.close();
}
static std::string hex(const std::string& data){
 const char* digits="0123456789abcdef";std::string result;result.reserve(data.size()*2);
 for(unsigned char c:data){result+=digits[c>>4];result+=digits[c&15];}return result;
}
struct SimulatedNvs {
 fs::path root;std::string pendingKey,pendingValue;
 RiscProvision::InstallTransport transport(){return {{this,[](void* ptr,const char* key,void* out,uint32_t cap,uint32_t* size){
   auto& s=*static_cast<SimulatedNvs*>(ptr);*size=0;auto path=s.root/key;
   try{if(fs::is_symlink(path))return RiscProvision::InputStatus::Invalid;
     if(!fs::exists(path))return RiscProvision::InputStatus::Missing;
     if(!fs::is_regular_file(path)||!fs::file_size(path)||fs::file_size(path)>cap)return RiscProvision::InputStatus::Invalid;
     std::ifstream in(path,std::ios::binary);in.read(static_cast<char*>(out),fs::file_size(path));if(!in)return RiscProvision::InputStatus::Unavailable;
     *size=uint32_t(in.gcount());return RiscProvision::InputStatus::Ready;
   }catch(...){return RiscProvision::InputStatus::Unavailable;}
 }},[](void* ptr,const char* key,const void* bytes,uint32_t size){auto& s=*static_cast<SimulatedNvs*>(ptr);s.pendingKey=key;s.pendingValue.assign(static_cast<const char*>(bytes),size);return true;},
 [](void* ptr){auto& s=*static_cast<SimulatedNvs*>(ptr);try{
   const auto pending=s.root/".install-pending";if(fs::exists(pending)||fs::is_symlink(pending))return false;
   write(pending,s.pendingValue);fs::rename(pending,s.root/s.pendingKey);s.pendingKey.clear();s.pendingValue.clear();return true;
 }catch(...){return false;}}};}
};
int main(int argc,char** argv){
 if(argc>1&&!strcmp(argv[1],"--install")){
   std::vector<char*> args{const_cast<char*>("python3"),const_cast<char*>(RISC_OWNER_INSTALL_SCRIPT),const_cast<char*>("--validator"),argv[0]};
   for(int i=2;i<argc;++i)args.push_back(argv[i]);
   args.push_back(nullptr);
   execvp(args[0],args.data());std::cerr<<"Owner installation client unavailable.\n";return 1;
 }

 bool installing=argc>1&&!strcmp(argv[1],"--install-sim");
 if(installing){--argc;++argv;}
 if(argc!=3&&argc!=4){std::cerr<<"usage: provision-input PROFILE_JSON NEW_OUTPUT_DIRECTORY [SNTP_SERVER]\n       provision-input --install-sim PROFILE_JSON EXISTING_SIM_NVS_DIRECTORY [SNTP_SERVER]\n";return 2;}
 // Files contain credentials supplied by the owner. Never print input/errors
 // carrying their bytes. Host process memory is not a secure secret vault.
 std::array<char,RiscProvision::ProfileInputBytes+1> bytes{};
 struct Wipe{decltype(bytes)& b;~Wipe(){for(volatile char& c:b)c=0;}} wipe{bytes};
 fs::path directory(argv[2]);bool created=false;
 try{
  std::ifstream in(argv[1],std::ios::binary);if(!in)throw 1;
  in.read(bytes.data(),bytes.size());auto size=in.gcount();
  if(in.bad()||!size||size>RiscProvision::ProfileInputBytes)throw 1;
  RiscProvision::Profile profile;
  if(!RiscProvision::parseProfile(bytes.data(),size,profile))throw 1;
  if(argc==4&&!RiscProvision::timeServer(argv[3]))throw 1;
  if(installing){
   if(!fs::is_directory(directory)||fs::is_symlink(directory))throw 1;
   const auto space=directory/"rte_bootstrap";
   if(fs::is_symlink(space))throw 1;
   if(!fs::exists(space)){fs::create_directory(space);fs::permissions(space,fs::perms::owner_all,fs::perm_options::replace);}
   if(!fs::is_directory(space)||(fs::status(space).permissions()&(fs::perms::group_all|fs::perms::others_all))!=fs::perms::none)throw 1;
   SimulatedNvs nvs{space,{},{} };std::array<char,RiscProvision::ProfileInputBytes> scratch{};
   const std::string time=argc==4?"{\"schema\":\"riscrte.sntp\",\"schema_version\":1,\"servers\":[\""+std::string(argv[3])+"\"]}":"";
   const auto result=RiscProvision::install(nvs.transport(),bytes.data(),uint32_t(size),time.data(),time.size(),scratch.data(),scratch.size());
   if(result==RiscProvision::InstallResult::Installed||result==RiscProvision::InstallResult::Unchanged){std::cout<<"Simulated NVS install complete; no device accessed.\n";return 0;}
   if(result==RiscProvision::InstallResult::SelectionUnknown){std::cerr<<"Simulated selector result uncertain; inspect state before retry.\n";return 3;}
   throw 1;
  }
  // Refuse existing paths, including symlinks; never replace an owner bundle.
  if(!fs::create_directory(directory))throw 1;
  created=true;
  fs::permissions(directory,fs::perms::owner_all,fs::perm_options::replace);
  const std::string descriptor="{\"schema\":\"riscrte.bootstrap\",\"schema_version\":1,\"profile_key\":\"profile\"}";
  const std::string input(bytes.data(),size);
  write(directory/"profile.bin",input);write(directory/"descriptor.bin",descriptor);
  // Espressif NVS partition generator CSV: blob encodings, never NVS strings.
  std::string csv="key,type,encoding,value\nrte_bootstrap,namespace,,\nprofile,data,hex2bin,"+hex(input)+"\ndescriptor,data,hex2bin,"+hex(descriptor)+"\n";
  if(argc==4){
   const std::string time="{\"schema\":\"riscrte.sntp\",\"schema_version\":1,\"servers\":[\""+std::string(argv[3])+"\"]}";
   write(directory/"time.bin",time);csv+="time,data,hex2bin,"+hex(time)+"\n";
  }
  write(directory/"nvs.csv",csv);
  write(directory/"COMPLETE","riscrte.bootstrap-input.v1\n");
  return 0;
 }catch(...){
  // Only the directory created by this invocation can be removed on failure.
  if(created){std::error_code error;fs::remove_all(directory,error);}
  std::cerr<<"Provisioning input packaging failed; no valid bundle produced.\n";return 1;
 }
}
