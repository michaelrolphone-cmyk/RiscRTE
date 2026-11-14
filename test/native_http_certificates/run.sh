#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
idf="${IDF_PATH:?Set IDF_PATH to the pinned ESP-IDF checkout}"
mbed="$idf/components/mbedtls/mbedtls"
test="$repo/test/native_http_certificates"
build="${BUILD_DIR:-$(mktemp -d)}"
mkdir -p "$build/compat/mbedtls"
for header in bignum ecp; do
 printf '#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS\n#include "mbedtls/private/%s.h"\n' "$header" > "$build/compat/mbedtls/$header.h"
done
cat > "$build/compat/host_config.h" <<'EOF'
#undef MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
EOF
cat > "$build/compat/crypto_config.h" <<'EOF'
#define MBEDTLS_PLATFORM_MEMORY
EOF
san=()
if [[ "${SANITIZE:-1}" == 1 ]]; then san=(-fsanitize=address -fno-omit-frame-pointer -g);fi
# Compile the pinned real X.509/PSA implementation with portable host crypto.
# Forwarding headers bridge IDF's private bignum/ecp include names only.
cmake -S "$mbed" -B "$build/crypto" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DGEN_FILES=OFF -DCMAKE_BUILD_TYPE=Debug \
 -DCMAKE_C_FLAGS="-I$build/compat ${san[*]}" -DMBEDTLS_USER_CONFIG_FILE="$build/compat/host_config.h" -DTF_PSA_CRYPTO_USER_CONFIG_FILE="$build/compat/crypto_config.h" > "$build/configure.log" 2>&1
cmake --build "$build/crypto" -- -j4 > "$build/build.log" 2>&1
python "$repo/scripts/native_idf_crt_bundle.py" "$idf/components/mbedtls/esp_crt_bundle/esp_crt_bundle.c" "$build/bundle.c"
(cd "$build" && python "$idf/components/mbedtls/esp_crt_bundle/gen_crt_bundle.py" --input "$idf/components/mbedtls/esp_crt_bundle/cacrt_all.pem")
include=(-I"$test/shim" -I"$idf/components/mbedtls/esp_crt_bundle/include" -I"$mbed/include" -I"$mbed/tf-psa-crypto/include" -I"$mbed/tf-psa-crypto/drivers/builtin/include" -I"$repo/src" -I"$repo/sdk/driver")
flags=("-DTF_PSA_CRYPTO_USER_CONFIG_FILE=\"$build/compat/crypto_config.h\"" -DMBEDTLS_DECLARE_PRIVATE_IDENTIFIERS -include mbedtls/private/ecdsa.h -DCONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=200 "-DMBEDTLS_USER_CONFIG_FILE=\"$build/compat/host_config.h\"")
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$build/untrusted.key" -out "$build/untrusted.pem" -days 30 -subj '/CN=capabilities-service.cantikash.chatgpt.site' > /dev/null 2>&1
for mode in before after; do
 extra=()
 if [[ "$mode" == after ]]; then extra=(-DCONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=1);fi
 cc -std=gnu11 -g "${san[@]}" "${include[@]}" "${flags[@]}" "${extra[@]}" -c "$build/bundle.c" -o "$build/bundle-$mode.o"
 c++ -std=c++17 -g "${san[@]}" "${include[@]}" "${flags[@]}" "${extra[@]}" "$test/check.cpp" "$build/bundle-$mode.o" -L"$build/crypto/library" -lmbedtls -lmbedx509 -ltfpsacrypto -o "$build/check-$mode"
 "$build/check-$mode" "$build/x509_crt_bundle" "$test/service-chain.pem" "$build/untrusted.pem"
done
