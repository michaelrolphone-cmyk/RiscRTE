#!/usr/bin/env python3
"""Compile main.cpp's exact handoff block with real bootstrap/time adapters."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.cpp').read_text()
start = source.index('  // Queued handoffs wait for an ordinary boot.')
end = source.index('\n#endif\n#if defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM)', start)
block = source[start:end]
assert source.index('NativeRealtime::start()') < start
assert block.index('NativeRetainedWake::coldBoot()') < block.index('RiscBootstrap::nvsInput()') < block.index('RiscProvisionRequest::bootInput(') < block.index('RiscBootstrap::run(')
assert block.index('RiscBootstrap::configuredFreshTime(') < block.index('RiscProvisionRequest::releaseBootInput()')
flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-DRISC_PAIRED_BANKS=1']
if os.environ.get('SANITIZE') == '1':
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g']
sources = ['test/native_provision_boot_gate_test.cpp', 'src/ports/esp32s3/NativeBootstrap.cpp',
           'src/ports/esp32s3/NativeSntp.cpp', 'src/ports/esp32s3/NativeRetainedWake.cpp',
           'src/runtime/provisioning/Coordinator.cpp', 'src/runtime/provisioning/BootstrapInput.cpp',
           'src/runtime/provisioning/Profile.cpp', 'src/bootstrap/Json.cpp']
with tempfile.TemporaryDirectory() as temp:
    build = Path(temp)
    (build / 'boot_handoff.inc').write_text(block + '\n')
    (build / 'esp_sleep.h').write_text('#pragma once\nenum {ESP_SLEEP_WAKEUP_UNDEFINED,ESP_SLEEP_WAKEUP_EXT0,ESP_SLEEP_WAKEUP_EXT1,ESP_SLEEP_WAKEUP_TIMER,ESP_SLEEP_WAKEUP_GPIO};\nint esp_sleep_get_wakeup_cause();\n')
    includes = [build, root / 'test/sntp_stubs', root / 'test/realtime_shim', root / 'test/retained_wake_shim',
                root / 'test/native_bank_stubs', root / 'src', root / 'lib/ArduinoJson/src',
                root / 'sdk/app', root / 'sdk/driver', root / 'sdk/hardware']
    count = 0
    for enabled, logs in [(1, 0), (1, 1), (0, 0)]:
        output = build / ('test-' + str(enabled) + '-' + str(logs))
        command = [os.environ.get('CXX', 'c++'), *flags, '-DRISC_NATIVE_PROVISION_REQUEST=' + str(enabled),
                   '-DRISC_STAGE_LOGS=' + str(logs), *['-I' + str(p) for p in includes],
                   *[str(root / p) for p in sources], '-lcrypto', '-o', str(output)]
        subprocess.run(command, check=True)
        modes = ['deep-pending', 'deep-absent', 'deep-absent-owner', 'deep-gpio-pending',
                 'deep-other-pending', 'deep-unset-pending', 'ordinary-pending', 'ordinary-absent',
                 'poweron-pending', 'poweron-absent-owner']
        if enabled:
            modes += ['deep-pending-then-ordinary']
        else:
            modes += ['deep-owner']
        for mode in modes:
            subprocess.run([str(output), mode], check=True)
            count += 1
print(f'PASS: {count} production-block boot cases; unchanged pending bytes; retained RTC; failed-download fallback; subsequent ordinary reset; legacy route')
