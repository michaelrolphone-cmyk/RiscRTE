#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "$here/async_fdset.c" -o "$build/async_fdset"
"$build/async_fdset"
