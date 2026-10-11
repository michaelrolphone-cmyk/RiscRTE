#!/usr/bin/env bash
set -eo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
openssl="${OPENSSL_ROOT:-/opt/homebrew/opt/openssl@3}"
flags=(-fsanitize=undefined -fno-sanitize-recover=all -I"$root/test/storage_admission_stubs" -I"$root/test/native_bank_stubs" -I"$root/lib/elf_loader/include" -I"$openssl/include")
cc -std=c11 "${flags[@]}" -c "$root/lib/elf_loader/src/esp_elf_validate.c" -o "$build/validate.o"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations "${flags[@]}" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/app" -I"$root/sdk/hardware" "$root/test/storage_admission_test.cpp" "$build/validate.o" -L"$openssl/lib" -lcrypto -o "$build/test"
"$build/test"
