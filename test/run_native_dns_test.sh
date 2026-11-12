#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/test/native_dns_shim" "$repo/test/native_dns_test.cpp" -Wl,--wrap=getaddrinfo,--wrap=freeaddrinfo -o "$build/dns"
"$build/dns"
