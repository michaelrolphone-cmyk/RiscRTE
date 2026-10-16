#!/usr/bin/env bash
# Build supported compiler/format fixtures and exercise their shared consumers.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Set IDF_PATH to the pinned ESP-IDF 6.1 SDK}"
case "${1:-}" in
 elf)
  : "${CC:=xtensa-esp32s3-elf-gcc}"
  "$CC" --version | head -1
  "$CC" --version | head -1 | grep -F 'esp-15.2.0_20251204'
  "$CC" --version | head -1 | grep -F '15.2.0'
  CC="$CC" python test/verify_legacy_runtime_header.py build/watch-clock-sdk/sdk/app/RiscRuntimeV1.h
  python scripts/build_apps.py --cc "$CC"
  bash test/run_performance_loader_test.sh
  SANITIZE=1 bash test/run_performance_loader_test.sh
  bash test/run_image_byte_cache_test.sh
  SANITIZE=1 bash test/run_image_byte_cache_test.sh
  bash test/run_elf_test.sh
  ;;
 spiffs)
  python test/prepare_native_spiffs.py "$IDF_PATH" build/native-spiffs-config
  export RISC_SPIFFS_NATIVE_IDF6=1
  spiffs="$IDF_PATH/components/spiffs/spiffs"
  producer="$IDF_PATH/components/spiffs/spiffsgen.py"
  image() { python "$producer" 0x510000 "$1" "$2" --page-size 256 --block-size 4096 --obj-name-len 32 --meta-len 4; }
  image build/store build/native-seed.bin
  image build/store build/native-seed-repeat.bin
  cmp build/native-seed.bin build/native-seed-repeat.bin
  python test/provision_seed_test.py -v
  python test/provisioning_spiffs_fixture.py build/spiffs-fixtures
  for shape in high-fill-83 boundary-3 boundary-128; do
   bash test/run_provisioning_spiffs_test.sh "$spiffs" "$IDF_PATH" build/native-seed.bin "build/spiffs-fixtures/$shape" "build/spiffs-result-$shape.bin"
  done
  SANITIZE=1 bash test/run_provisioning_spiffs_test.sh "$spiffs" "$IDF_PATH" build/native-seed.bin build/spiffs-fixtures/high-fill-83 build/spiffs-result-high-fill-83-sanitized.bin
  bash test/run_provisioning_spiffs_image_test.sh "$spiffs" "$IDF_PATH" build/native-seed.bin build/store
  python test/provisioning_spiffs_image_fixture.py build/compact-image-fixture
  image build/compact-image-fixture build/compact-image.bin
  bash test/run_store_image_capacity_test.sh build/compact-image.bin
  bash test/run_provisioning_spiffs_image_test.sh "$spiffs" "$IDF_PATH" build/compact-image.bin build/compact-image-fixture
  SANITIZE=1 bash test/run_provisioning_spiffs_image_test.sh "$spiffs" "$IDF_PATH" build/compact-image.bin build/compact-image-fixture
  ;;
 *) echo "usage: $0 elf|spiffs" >&2; exit 2;;
esac
