#pragma once
#include "Json.h"
#include <RiscKeyValueV1.h>
#include <RiscBoundKeyValueV1.h>
#include <RiscBoundAppDataV1.h>

namespace RiscBoot {
// Declarations are not grants. An optional row may have no owner-authorized
// match, but any supplied match undergoes the same binding checks as required.
// Borrowed JSON views add no persistent per-app storage or target ABI fields.
class AppDeclarations {
 public:
  explicit AppDeclarations(JsonObjectConst manifest):required_(manifest["requires"]),optional_(manifest["optional"]){}
  size_t size()const{return required_.size()+optional_.size();}
  bool required(size_t i)const{return i<required_.size();}
  JsonObjectConst operator[](size_t i)const{
    return required(i)?required_[i].as<JsonObjectConst>():optional_[i-required_.size()].as<JsonObjectConst>();
  }
  const char* error(size_t maximum)const{
    if(!required_.is<JsonArrayConst>() ||
       (!optional_.isUnbound() && !optional_.is<JsonArrayConst>()) || size()>maximum)
      return "invalid app identity/declarations";
    for(size_t i=0;i<size();++i){
      const auto row=(*this)[i];char capability[96];int64_t api=0;
      if(!keys(row,{"capability","api"}) || !text(row["capability"],capability,sizeof(capability)) ||
         !integer(row["api"],1,UINT32_MAX,api))return "invalid app requirement";
      if(!strcmp(capability,RISC_BOUND_KEY_VALUE_CAPABILITY))return "provider key-value denied to app";
      if(!strcmp(capability,RISC_BOUND_APP_DATA_CAPABILITY))return "provider app-data denied to app";
      for(size_t j=0;j<i;++j)if(eq((*this)[j]["capability"],capability) &&
          (strcmp(capability,RISC_KEY_VALUE_CAPABILITY) || (*this)[j]["api"].as<uint32_t>()==uint32_t(api)))
        return "duplicate app requirement";
    }
    return nullptr;
  }
 private:
  JsonVariantConst required_,optional_;
};
}
