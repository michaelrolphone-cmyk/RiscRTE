"""Compile production HTTP and CPU facade against the installed ESP32-S3 SDK."""
from pathlib import Path
import argparse
import json
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--packages', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
r = Path(__file__).resolve().parents[1]
sdk = a.packages / 'framework-arduinoespressif32/tools/sdk/esp32s3'
prefix = a.packages / 'toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-'
a.output.mkdir(parents=True, exist_ok=True)
includes = [r / 'src', r / 'sdk/app', r / 'sdk/driver', r / 'sdk/hardware',
            r / 'lib/ArduinoJson/src', sdk / 'qio_opi/include',
            sdk / 'include/freertos/include/esp_additions',
            sdk / 'include/freertos/include/esp_additions/freertos',
            sdk / 'include/freertos/port/xtensa/include/freertos',
            sdk / 'include/lwip/include/apps', sdk / 'include/lwip/lwip/src/include',
            sdk / 'include/lwip/port/esp32/include', sdk / 'include/esp_hw_support/include/soc',
            sdk / 'include/esp-tls']
for d in sorted((sdk / 'include').rglob('*')):
    if d.is_dir() and (d.name in ('include', 'platform_include') or
                      str(d).endswith(('/include/esp32s3', '/include/xtensa', '/port/xtensa/include'))):
        includes.append(d)
flags = ['-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Os',
         '-mlongcalls', '-fstack-usage', '-DESP_PLATFORM', '-DRISC_ENABLE_HTTP=1',
         '-DMBEDTLS_CONFIG_FILE="mbedtls/esp_config.h"']
inc = ['-I' + str(d) for d in includes]
commands = []
def run(command):
    commands.append(command)
    result = subprocess.run(command)
    if result.returncode:
        raise SystemExit(result.returncode)
run([str(prefix) + 'gcc', '-std=c11', *flags, *inc, '-c',
     str(r / 'test/http_time_headers_abi.c'), '-o', str(a.output / 'http_abi.o')])
for source, name in [('test/native_http_target_compile.cpp', 'http'),
                     ('src/ports/esp32s3/CpuPort.cpp', 'cpu_port'),
                     ('src/ports/esp32s3/NativeHardware.cpp', 'native_hardware')]:
    run([str(prefix) + 'g++', '-std=gnu++17', '-fno-exceptions', '-fno-rtti',
         *flags, *inc, '-c', str(r / source), '-o', str(a.output / (name + '.o'))])
symbols = subprocess.check_output([str(prefix) + 'nm', '-u', str(a.output / 'http.o')], text=True)
(a.output / 'http.undefined.txt').write_text(symbols)
# Read-only monotonic timing and private UDP are expected. An independent
# object audit refuses every wall-clock write, global SNTP service, or RTC use.
assert not re.search(r'settimeofday|adjtime|clock_settime|sntp|rtc|Realtime', symbols, re.I), symbols
for required in ['esp_fill_random', 'getaddrinfo', 'recvfrom', 'sendto', 'esp_tls_init']:
    assert required in symbols, (required, symbols)
(a.output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
print(symbols)
print('PASS: real ESP32-S3 C ABI, production SNTP/HTTPS CPU suffix and NativeHardware integration objects; no clock writes/global SNTP/RTC imports. No firmware link or device qualification.')
