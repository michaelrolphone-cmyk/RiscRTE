#include "ports/esp32s3/HttpCertificate.h"
#include <esp_crt_bundle.h>
#include <psa/crypto.h>
#include <mbedtls/platform.h>
#include <map>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#ifdef NDEBUG
#error Certificate verification tests require active assertions
#endif
using namespace RiscCpu;
extern "C" {
extern const unsigned char bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const unsigned char bundle_end[] asm("_binary_x509_crt_bundle_end");
const unsigned char bundle_start[]={0};
const unsigned char bundle_end[]={0};
}
static std::map<void*,size_t> allocations;
static unsigned allocationAttempt=0, failAllocation=0, allocationFailures=0;
static void* countedCalloc(size_t count,size_t bytes){
 if(failAllocation && ++allocationAttempt==failAllocation){++allocationFailures;return nullptr;}
 void* p=std::calloc(count,bytes);if(p)allocations.emplace(p,count*bytes);return p;
}
static void countedFree(void* p){if(p){assert(allocations.erase(p)==1);std::free(p);}}
struct Context {
 HttpCertificate::BundleCandidates candidates;
 int(*verify)(void*,mbedtls_x509_crt*,int,uint32_t*)=nullptr;
 void* verifyContext=nullptr;
 uint64_t utc=1793404800; // 2026-10-31 UTC; fixture is valid at capture.
 unsigned anchors=0;
 bool reproduceOldTime=false;
};
static int verify(void* raw,mbedtls_x509_crt* crt,int depth,uint32_t* flags){
 auto& c=*static_cast<Context*>(raw);
 const int result=c.verify(c.verifyContext,crt,depth,flags);
 const bool anchor=c.candidates.anchor(crt);
 if(anchor)++c.anchors;
 if(!HttpCertificate::validAt(*crt,*flags,c.utc,anchor&&!c.reproduceOldTime))return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
 return result;
}
static int check(const char* path,const char* host,int mutation,uint32_t& flags,unsigned& anchors,unsigned fail=0){
 mbedtls_x509_crt chain;mbedtls_x509_crt_init(&chain);
 assert(mbedtls_x509_crt_parse_file(&chain,path)==0);
 if(mutation==1)chain.valid_to.year=2025;
 if(mutation==2)chain.valid_from.year=2027;
 if(mutation==3){assert(chain.next);chain.next->valid_to.year=2025;}
 if(mutation==4){assert(chain.next);chain.next->MBEDTLS_PRIVATE(sig).p[0]^=1;}
 if(mutation==5)chain.valid_from=chain.valid_to={};
 mbedtls_ssl_config config;mbedtls_ssl_config_init(&config);
 assert(esp_crt_bundle_attach(&config)==0);
 Context c;c.verify=config.MBEDTLS_PRIVATE(f_vrfy);c.verifyContext=config.MBEDTLS_PRIVATE(p_vrfy);
 assert(c.verify);
 int result;allocationAttempt=0;failAllocation=fail;
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
 assert(c.candidates.bind(&config));
 if(mutation==6)c.utc=0;
 if(mutation==7)c.reproduceOldTime=true;
 result=mbedtls_x509_crt_verify_with_ca_cb(&chain,config.MBEDTLS_PRIVATE(f_ca_cb),config.MBEDTLS_PRIVATE(p_ca_cb),&mbedtls_x509_crt_profile_default,host,&flags,verify,&c);
#else
 result=mbedtls_x509_crt_verify(&chain,config.MBEDTLS_PRIVATE(ca_chain),nullptr,host,&flags,verify,&c);
#endif
 failAllocation=0;anchors=c.anchors;
 mbedtls_x509_crt_free(&chain);mbedtls_ssl_config_free(&config);
 return result;
}
int main(int argc,char** argv){
 assert(argc==4);assert(mbedtls_platform_set_calloc_free(countedCalloc,countedFree)==0);assert(psa_crypto_init()==PSA_SUCCESS);
 std::ifstream in(argv[1],std::ios::binary);std::vector<unsigned char> bundle((std::istreambuf_iterator<char>(in)),{});
 assert(!bundle.empty()&&esp_crt_bundle_set(bundle.data(),bundle.size())==0);
 uint32_t flags=0;unsigned anchors=0;
 const char* host="capabilities-service.cantikash.chatgpt.site";
 const int result=check(argv[2],host,0,flags,anchors);
 std::printf("service chain: result=%d flags=%u anchors=%u\n",result,flags,anchors);
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
 assert(result==0&&flags==0&&anchors==1);
 assert(check(argv[2],"wrong.example",0,flags,anchors)!=0&&(flags&MBEDTLS_X509_BADCERT_CN_MISMATCH));
 for(int mutation:{1,2,3,4,5,6,7}){
  const int rc=check(argv[2],host,mutation,flags,anchors);
  std::printf("negative mutation=%d result=%d flags=%u\n",mutation,rc,flags);assert(rc!=0);
  if(mutation==1||mutation==3||mutation==5||mutation==7)assert(flags&MBEDTLS_X509_BADCERT_EXPIRED);
  if(mutation==2)assert(flags&MBEDTLS_X509_BADCERT_FUTURE);
 }
 assert(check(argv[3],host,0,flags,anchors)!=0);
 const size_t baseline=allocations.size();
 for(unsigned i=0;i<100;++i){assert(check(argv[2],host,0,flags,anchors)==0&&flags==0&&anchors==1);assert(allocations.size()==baseline);}
 for(unsigned failure=1;failure<100;++failure){
  const unsigned before=allocationFailures;const int rc=check(argv[2],host,0,flags,anchors,failure);
  assert(allocations.size()==baseline);
  if(allocationFailures==before){assert(rc==0);break;}
  assert(rc!=0);
 }
 assert(allocationFailures>10);std::printf("checked allocation failure positions=%u\n",allocationFailures);
 std::puts("PASS real SDK chain, hostname, validity, trust, signature, private clock and repeated cleanup");
#else
 assert(result==MBEDTLS_ERR_X509_FATAL_ERROR);
 std::puts("REPRODUCED v1.2.3 failure: exact -0x3000 with captured public service chain");
#endif
 mbedtls_psa_crypto_free();assert(allocations.empty());
}
