#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)";trap 'rm -rf "$work"' EXIT
for configuration in ${APP_CATALOG_CONFIGURATIONS:-default atomic lifecycle atomic-lifecycle};do
build="$work/$configuration";mkdir -p "$build"
flags=();if [[ "${SANITIZE:-0}" == 1 ]];then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
case "$configuration" in
  default) features=(-DRISC_RUNTIME_ATOMIC_KEY_VALUE=0 -DRISC_NATIVE_SETTINGS_LIFECYCLE=0);;
  atomic) features=(-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 -DRISC_NATIVE_SETTINGS_LIFECYCLE=0);;
  lifecycle) features=(-DRISC_RUNTIME_ATOMIC_KEY_VALUE=0 -DRISC_NATIVE_SETTINGS_LIFECYCLE=1);;
  atomic-lifecycle) features=(-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 -DRISC_NATIVE_SETTINGS_LIFECYCLE=1);;
  *) echo "Unknown catalog feature configuration: $configuration" >&2;exit 2;;
esac
echo "app.catalog feature configuration: $configuration"
shared=();host=(-rdynamic);if [[ "$(uname)" == Darwin ]];then shared=(-Wl,-undefined,dynamic_lookup);fi
for app in default child;do
  extra=();if [[ "$app" == child ]];then extra=(-DCATALOG_CHILD);fi
  cc "${flags[@]}" -std=c11 -Wall -Wextra -Werror -shared -fPIC -fvisibility=hidden "${shared[@]}" "${extra[@]}" -I"$repo/sdk/app" "$repo/test/fixtures/app_catalog_app.c" -o "$build/$app.elf"
done
cc "${flags[@]}" -std=c11 -Wall -Wextra -Werror -shared -fPIC -fvisibility=hidden "${shared[@]}" -I"$repo/sdk/app" -I"$repo/sdk/driver" "$repo/test/fixtures/app_catalog_provider.c" -o "$build/sd.elf"
c++ "${flags[@]}" "${features[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers "${host[@]}" \
 -Wl,--wrap=fopen,--wrap=fread,--wrap=opendir \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/app_catalog_runtime_test.cpp" -ldl -o "$build/test"
for mode in normal unauthorized empty normalized_metadata long_path retained acquire_retained acquire_lost_owner;do "$build/test" "$build" "$mode";done
done
