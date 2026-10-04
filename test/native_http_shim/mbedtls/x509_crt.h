#pragma once
#include <cstdint>
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED -0x2700
#define MBEDTLS_X509_BADCERT_FUTURE 1
#define MBEDTLS_X509_BADCERT_EXPIRED 2
struct mbedtls_x509_time {int year,mon,day,hour,min,sec;};
struct mbedtls_x509_crt {mbedtls_x509_time valid_from{},valid_to{};};
