#pragma once
#include "runtime/provisioning/RequestQueue.h"
#include <nvs.h>
#include <cstring>
// Private storage adapter shared by the native bridge and upstream-NVS tests.
// Control is one 577-byte NVS string; only the one-byte state updates in place.
namespace RiscProvisionRequestStorage {
using RiscProvision::InputStatus;
constexpr size_t ControlChars=RiscProvision::RequestReceiptBytes*2+1;
inline bool receipt(const char*key){return key&&!strcmp(key,"receipt");}
inline bool payload(const char*key){return key&&!strcmp(key,"request");}
inline bool marker(const char*key){return key&&!strcmp(key,"state");}
// In IDF 4.4.7 a typed getter can report NOT_FOUND for an existing multipart
// blob of the wrong type. Consult metadata before interpreting absence. A
// failed iterator allocation on a populated namespace is unavailable, not empty.
inline InputStatus kind(nvs_handle_t h,const char*key,nvs_type_t&found){
 if(!receipt(key)&&!payload(key)&&!marker(key))return InputStatus::Invalid;
 size_t entries=0;
 if(nvs_get_used_entry_count(h,&entries)!=ESP_OK)return InputStatus::Unavailable;
 if(!entries)return InputStatus::Missing;
 auto it=nvs_entry_find("nvs","rte_provreq",NVS_TYPE_ANY);
 if(!it)return InputStatus::Unavailable;
 while(it){
  nvs_entry_info_t info{};nvs_entry_info(it,&info);
  if(!strcmp(info.key,key)){
   found=info.type;nvs_release_iterator(it);return InputStatus::Ready;
  }
  it=nvs_entry_next(it);
 }
 return InputStatus::Missing;
}
inline InputStatus probe(nvs_handle_t h,const char*key){nvs_type_t type=NVS_TYPE_ANY;return kind(h,key,type);}
inline InputStatus read(nvs_handle_t h,const char*key,void*out,uint32_t cap,uint32_t*actual){
 if(actual)*actual=0;
 if(!out||!actual)return InputStatus::Invalid;
 nvs_type_t type=NVS_TYPE_ANY;auto present=kind(h,key,type);
 if(present!=InputStatus::Ready)return present;
 const auto expected=marker(key)?NVS_TYPE_U8:receipt(key)?NVS_TYPE_STR:NVS_TYPE_BLOB;
 if(type!=expected)return InputStatus::Invalid;
 if(marker(key)){
  if(cap<1)return InputStatus::Invalid;
  uint8_t value=0;auto rc=nvs_get_u8(h,key,&value);
  if(rc!=ESP_OK)return InputStatus::Unavailable;
  if(value!=RiscProvision::RequestReady&&value!=RiscProvision::RequestActive)return InputStatus::Invalid;
  *static_cast<uint8_t*>(out)=value;*actual=1;return InputStatus::Ready;
 }
 size_t n=0;auto rc=receipt(key)?nvs_get_str(h,key,nullptr,&n):nvs_get_blob(h,key,nullptr,&n);
 if(rc!=ESP_OK)return InputStatus::Unavailable;
 if(receipt(key)){
  if(n!=ControlChars||cap<RiscProvision::RequestReceiptBytes)return InputStatus::Invalid;
  char text[ControlChars];rc=nvs_get_str(h,key,text,&n);
  if(rc!=ESP_OK||n!=ControlChars||text[ControlChars-1])return InputStatus::Invalid;
  auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
  auto*p=static_cast<uint8_t*>(out);
  for(size_t i=0;i<RiscProvision::RequestReceiptBytes;++i){
   int a=digit(text[2*i]),b=digit(text[2*i+1]);
   if(a<0||b<0)return InputStatus::Invalid;
   p[i]=uint8_t((a<<4)|b);
  }
  *actual=RiscProvision::RequestReceiptBytes;return InputStatus::Ready;
 }
 if(!n||n>cap)return InputStatus::Invalid;
 size_t expectedSize=n;rc=nvs_get_blob(h,key,out,&n);
 if(rc!=ESP_OK||n!=expectedSize)return InputStatus::Invalid;
 *actual=uint32_t(n);return InputStatus::Ready;
}
inline bool set(nvs_handle_t h,const char*key,const void*bytes,uint32_t n){
 if(!bytes)return false;
 if(marker(key)){
  if(n!=1)return false;
  const auto value=*static_cast<const uint8_t*>(bytes);
  return (value==RiscProvision::RequestReady||value==RiscProvision::RequestActive)&&nvs_set_u8(h,key,value)==ESP_OK;
 }
 if(probe(h,key)!=InputStatus::Missing)return false;
 if(payload(key))return nvs_set_blob(h,key,bytes,n)==ESP_OK;
 if(!receipt(key)||n!=RiscProvision::RequestReceiptBytes)return false;
 static const char hex[]="0123456789abcdef";char text[ControlChars];auto*p=static_cast<const uint8_t*>(bytes);
 for(size_t i=0;i<n;++i){text[2*i]=hex[p[i]>>4];text[2*i+1]=hex[p[i]&15];}
 text[ControlChars-1]=0;return nvs_set_str(h,key,text)==ESP_OK;
}
inline bool erase(nvs_handle_t h,const char*key){
 if(!receipt(key)&&!payload(key))return false;
 auto rc=nvs_erase_key(h,key);return rc==ESP_OK||rc==ESP_ERR_NVS_NOT_FOUND;
}
}
