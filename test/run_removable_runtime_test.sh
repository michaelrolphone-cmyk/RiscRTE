#!/usr/bin/env bash
set -eo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-g)
shared_flags=()
host_flags=(-rdynamic)
if [[ "$(uname -s)" == Darwin ]]; then shared_flags=(-Wl,-undefined,dynamic_lookup); host_flags=(-Wno-unused-private-field -Wno-unused-but-set-variable); fi
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=${SANITIZERS:-address,undefined} -fno-omit-frame-pointer -fno-sanitize-recover=all); fi
for name in default default2 previous revision1 revision2; do
  extra=();if [[ "$name" == default ]]; then extra=(-DREMOVABLE_DEFAULT); elif [[ "$name" == default2 ]]; then extra=(-DREMOVABLE_DEFAULT -DIMAGE_REVISION=2); elif [[ "$name" == previous ]]; then extra=(-DREMOVABLE_PREVIOUS); elif [[ "$name" == revision2 ]]; then extra=(-DIMAGE_REVISION=2); fi
  cc "${flags[@]}" -std=c11 -shared "${shared_flags[@]}" -fPIC -fvisibility=hidden -I"$repo/sdk/app" "${extra[@]}" "$repo/test/fixtures/removable_app.c" -o "$build/$name.elf"
done
cc "${flags[@]}" -std=c11 -shared "${shared_flags[@]}" -fPIC -fvisibility=hidden -I"$repo/sdk/app" -I"$repo/sdk/driver" "$repo/test/fixtures/removable_provider.c" -o "$build/sd.elf"
cc "${flags[@]}" -std=c11 -shared "${shared_flags[@]}" -fPIC -fvisibility=hidden -DINTERNAL_PROVIDER -I"$repo/sdk/app" -I"$repo/sdk/driver" "$repo/test/fixtures/removable_provider.c" -o "$build/internal.elf"
c++ "${flags[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers "${host_flags[@]}" -DRISC_REMOVABLE_APP_TEST=1 \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" \
  -I"$repo/test/drivers/stubs" -I"$repo/test/support/native_registry/stubs" -I"$repo/lib/elf_loader/include" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/removable_runtime_test.cpp" -ldl -lcrypto -o "$build/test"
cc "${flags[@]}" -std=c11 -shared "${shared_flags[@]}" -fPIC -fvisibility=hidden -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -DFIXTURE_TYPED -DFIXTURE_ID='"sd-plugin"' -DFIXTURE_CAPABILITY='"test.plugin"' "$repo/test/drivers/provider_graph_fixture.c" -o "$build/plugin.elf"
cc "${flags[@]}" -std=c11 -shared "${shared_flags[@]}" -fPIC -fvisibility=hidden -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -DFIXTURE_VALUE=43 -DFIXTURE_TYPED -DFIXTURE_ID='"sd-plugin"' -DFIXTURE_CAPABILITY='"test.plugin"' "$repo/test/drivers/provider_graph_fixture.c" -o "$build/plugin2.elf"
for mode in files-provider-core-match files-provider-core-substitute files-provider-core-substitute-close-retained files-provider-core-substitute-owner-retained files-provider-core-substitute-storage-retained files-core-wrong-hash files-core-wrong-hash-close-retained files-core-present files-core-absent files-core-busy files-core-removed files-core-malformed files-core-relocation files-core-close-retained files-core-owner-retained files-core-storage-retained files-core-default-present files-core-default-absent files-core-opt-out files-core-opt-out-absent provider-core-match provider-core-substitute provider-core-substitute-close-retained provider-core-substitute-owner-retained provider-core-substitute-storage-retained provider-core-substitute-close-owner-retained provider-core-substitute-close-storage-retained provider-core-legacy provider-core-external core-wrong-hash core-wrong-hash-close-retained core-wrong-hash-release-retained core-present core-absent core-busy core-removed core-malformed core-relocation core-close-retained core-release-retained core-owner-retained core-storage-retained core-close-owner-retained core-internal-missing core-internal-corrupt core-default-present core-default-absent core-opt-out core-opt-out-absent core-policy core-dynamic internal-only files-ambiguous-app files-provider-ambiguous files-normal files-replacement files-absent files-missing files-busy files-removed files-malformed files-close-retained files-provider-source files-provider-fallback files-provider-required files-provider-retained files-provider-bootstrap catalog-media shared-settings catalog-settings-scalable catalog-settings-legacy catalog-settings catalog-providers provider-source provider-fallback provider-required provider-retained provider-bootstrap catalog-authority catalog-resident catalog discovery normal replacement absent missing busy removed malformed close-retained invalid-path default-external; do
  "$build/test" "$build" "$mode"
done
