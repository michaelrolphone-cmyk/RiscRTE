#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${OUTPUT_DIR:-$(mktemp -d)}";mkdir -p "$build"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -g -O1)
if [[ ${SANITIZE:-0} == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
c++ "${flags[@]}" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" "$root/test/file_source_provider_test.cpp" -o "$build/test"
"$build/test"
# Consumer object may depend on generic callbacks, never native file operations.
if nm -u "$build/test" | grep -E ' (open|close|read|write|unlink|lseek|fstat|fsync)(@|$)';then exit 1;fi
if grep -E '#include <(fcntl.h|unistd.h|sys/stat.h)>|::(open|close|read|write|unlink|lseek|fstat|fsync)\(' "$root/src/runtime/storage/FileSourceFiles.h";then exit 1;fi
