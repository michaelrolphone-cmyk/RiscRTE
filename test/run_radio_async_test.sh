#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
watch="${WATCH_SOURCE:-/workspace/shared/watch-wifi-async-20261010}"
build="${BUILD_DIR:-$(mktemp -d)}";mkdir -p "$build"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unused-function -g -pthread -DRISC_STAGE_LOGS="${STAGE_LOGS:-1}")
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
if [[ "${TSAN:-0}" == 1 ]]; then san=(-fsanitize=thread -fno-omit-frame-pointer -no-pie);fi
inc=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" -I"$repo/test/native_radio_shim" -I"$repo/test/native_radio_async_task_shim" -I"$watch/include" -I"$watch/sdk/driver" -I"$watch/drivers/twatch_wifi")
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${san[@]}" -I"$watch/include" -I"$watch/sdk/driver" -Dt5_driver_get=production_wifi_get -c "$watch/drivers/twatch_wifi/driver.c" -o "$build/wifi.o"
extra=()
if [[ -n "${APP_WORKFLOW_OBJECT:-}" ]]; then extra=(-DAPP_WORKFLOW_OBJECT "$APP_WORKFLOW_OBJECT");
 if [[ "${APP_WORKFLOW_UNAVAILABLE:-0}" == 1 ]];then extra+=(-DAPP_WORKFLOW_UNAVAILABLE);fi
fi
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${inc[@]}" "${sources[@]}" "$repo/test/native_radio_async_test.cpp" "$build/wifi.o" "${extra[@]}" -ldl -o "$build/async"
cases=(workspace-allocation-failure startup-failure startup-unavailable suffix:size suffix:tag suffix:version suffix:callback suffix:service invalid before-dequeue repeated concurrent terminal-contention service-lease resource-lease legacy copied contention allocation complete-cancel)
for stage in get_mode netif_init loop_create netif_new attach defaults register wifi_init storage_ram station_mode credentials_copy start connect scan_start records;do cases+=("setup:$stage");done
for stage in scan_stop list_clear disconnect credentials_clear stop deinit unregister driver_clear loop_delete dhcp_stop netif_stop netif_destroy;do cases+=("cleanup:$stage");done
for stage in scan_stop list_clear disconnect credentials_clear stop deinit unregister driver_clear loop_delete dhcp_stop;do cases+=("failure:$stage");done
if [[ -n "${APP_WORKFLOW_OBJECT:-}" ]];then cases+=(app-workflow);
 if [[ "${APP_WORKFLOW_UNAVAILABLE:-0}" == 1 ]];then cases+=(app-workflow-unavailable);fi
fi
for case in "${cases[@]}";do "$build/async" "$case";done
