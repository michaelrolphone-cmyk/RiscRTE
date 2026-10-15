#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -I"$repo/sdk/app" -I"$repo/src")
for language in c c++;do
 if [[ "$language" == c ]];then compiler=cc;standard=c11;else compiler=c++;standard=c++17;fi
 for selection in default selected;do
  selected=();if [[ "$selection" == selected ]];then selected=(-DSTORAGE_CONTRACT_OPT_IN -DRISC_RUNTIME_PREFERENCE_STORE=1 -DRISC_RUNTIME_STORAGE_FILES=1);fi
  "$compiler" -x "$language" -std="$standard" "${flags[@]}" "${selected[@]}" "$repo/test/storage_contract_abi_test.c" -o "$build/abi"
  "$build/abi"
 done
done
for option in RISC_RUNTIME_PREFERENCE_STORE RISC_RUNTIME_STORAGE_FILES;do
 if cc -std=c11 "${flags[@]}" -D"$option"=2 -fsyntax-only "$repo/test/storage_contract_abi_test.c" >"$build/invalid.log" 2>&1;then echo "invalid opt-in accepted: $option" >&2;exit 1;fi
done
echo 'Optional generic storage contracts: C11/C++17 ABI, explicit opt-in and invalid selection PASS'
