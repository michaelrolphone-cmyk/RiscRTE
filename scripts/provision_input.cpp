// Offline owner input packaging. No network, device, NVS write or erase API.
#include "runtime/provisioning/Profile.h"
#include "runtime/provisioning/BootstrapInput.h"
#include "runtime/provisioning/TimeInput.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <array>
namespace fs=std::filesystem;
static void write(const fs::path& path,const std::string& data){
 std::ofstream out(path,std::ios::binary);out.exceptions(std::ios::badbit|std::ios::failbit);
 out.write(data.data(),data.size());out.close();
}
static std::string hex(const std::string& data){
 const char* digits="0123456789abcdef";std::string result;result.reserve(data.size()*2);
 for(unsigned char c:data){result+=digits[c>>4];result+=digits[c&15];}return result;
}
int main(int argc,char** argv){
 if(argc!=3&&argc!=4){std::cerr<<"usage: provision-input PROFILE_JSON NEW_OUTPUT_DIRECTORY [SNTP_SERVER]\n";return 2;}
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
