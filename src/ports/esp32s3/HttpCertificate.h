#pragma once
#include "HttpBounds.h"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

namespace RiscCpu { namespace HttpCertificate {
// The bundle's CA callback constructs a trust anchor from its subject/key.
// It is not a peer certificate and has no encoded validity interval. Remember
// its actual pointer, rather than trusting an empty date on an arbitrary cert.
class BundleCandidates {
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
  mbedtls_x509_crt_ca_cb_t callback_=nullptr;
  void* context_=nullptr;
  const mbedtls_x509_crt* candidate_=nullptr;
  int failure_=0;
  static int lookup(void* context,const mbedtls_x509_crt* child,mbedtls_x509_crt** out){
    auto* self=static_cast<BundleCandidates*>(context);
    self->candidate_=nullptr;
    const int result=self->callback_(self->context_,child,out);
    self->failure_=result;
    if(result==0)self->candidate_=*out;
    return result;
  }
#endif
public:
  bool bind(mbedtls_ssl_config* config){
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
    candidate_=nullptr;
    failure_=0;
    callback_=config->MBEDTLS_PRIVATE(f_ca_cb);
    context_=config->MBEDTLS_PRIVATE(p_ca_cb);
    if(!callback_)return false;
    mbedtls_ssl_conf_ca_cb(config,lookup,this);
#else
    (void)config;
#endif
    return true;
  }
  int failure()const{
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
    return failure_;
#else
    return 0;
#endif
  }
  bool anchor(const mbedtls_x509_crt* cert)const{
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
    return cert && cert==candidate_ && !cert->raw.p &&
      cert->valid_from.year==0 && cert->valid_to.year==0;
#else
    (void)cert;return false;
#endif
  }
};
inline int compare(const mbedtls_x509_time& a,const HttpBounds::Calendar& b){
  const int left[]={a.year,a.mon,a.day,a.hour,a.min,a.sec};
  const int right[]={b.year,b.month,b.day,b.hour,b.minute,b.second};
  for(unsigned i=0;i<6;++i){if(left[i]<right[i])return -1;if(left[i]>right[i])return 1;}return 0;
}
inline bool validAt(const mbedtls_x509_crt& cert,uint32_t& flags,uint64_t seconds,bool anchor){
  HttpBounds::Calendar utc{};
  if(!HttpBounds::calendar(seconds,utc))return false;
  // All real certificates retain explicit private-clock checks. Never clear
  // hostname, signature, trust or existing validity errors here.
  if(!anchor){
    if(compare(cert.valid_from,utc)>0)flags|=MBEDTLS_X509_BADCERT_FUTURE;
    if(compare(cert.valid_to,utc)<0)flags|=MBEDTLS_X509_BADCERT_EXPIRED;
  }
  return true;
}
} }
