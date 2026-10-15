#!/usr/bin/env python3
"""Generate/check an opt-in trusted policy for one experimental native-IDF6 ELF.

Qualification metadata is checked evidence, never authority. Only an explicit
trusted firmware inclusion can select the generated policy. Generation does not
activate the controller or establish board/physical hardware qualification.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct

from elftools.elf.elffile import ELFFile
from elftools.common.exceptions import ELFError
from audit_provider_diagnostics import address_bytes, text_at

PINNED_SHA256 = '9de7e76d83bd984c76263b863de715cc5058d6fc21387fb728c5aa3a1441e294'
PINNED_SIZE = 792696
DRIVER_ID = 'usb-controller-esp32s3'
VERSION = '0.1.25'
CAPABILITY = 'usb.controller'
OS_CPU_ABI = 2
DIAGNOSTIC_ABI = 1
PROVIDER_ABI = 2  # Public descriptor ABI, independently versioned from OS_CPU_ABI.
PROVIDER_SIZE = 40
NATIVE_COHORT = 'esp32s3-idf6.1-gcc15.2.0-20251204-v1'
IDF_COMMIT = 'fff9895c82d744c7237be8847347bdd1b07c6643'
USB_COMMIT = '0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a'
IMPORT_FILE_SHA256 = 'a21d1ca8d39b47b994068074f668ce7c71f8a8247711681ad4cf8bcfc9bf395e'
IMPORTS = (
    '__assert_func', '_esp_error_check_failed', '_frxt_setup_switch', 'abort',
    'calloc', 'esp_intr_alloc', 'esp_intr_disable', 'esp_intr_enable',
    'esp_intr_free', 'esp_log', 'esp_log_default_level', 'esp_log_timestamp',
    'esp_rom_delay_us', 'esp_rom_printf', 'free', 'heap_caps_aligned_calloc',
    'heap_caps_calloc', 'heap_caps_free', 'heap_caps_malloc', 'memcpy', 'memset',
    'periph_rcc_enter', 'periph_rcc_exit', 'printf', 'putchar', 'puts', 'strcmp',
    'ulTaskGenericNotifyTake', 'uxQueueMessagesWaiting', 'vPortExitCritical',
    'vQueueDelete', 'vTaskDelay', 'vTaskGenericNotifyGiveFromISR',
    'xPortEnterCriticalTimeout', 'xPortInIsrContext', 'xQueueCreateMutex',
    'xQueueGenericCreate', 'xQueueGenericSend', 'xQueueGiveFromISR',
    'xQueueReceive', 'xQueueSemaphoreTake', 'xTaskGetCoreID',
    'xTaskGetCurrentTaskHandle', 'xTaskGetTickCount',
)
REQUIREMENTS = [{'capability': 'board.power.vbus', 'api': 1}]
USB_CONFIG = {
    'CONTROL_TRANSFER_MAX_SIZE': 256, 'HW_BUFFER_BIAS_BALANCED': 1,
    'DEBOUNCE_DELAY_MS': 250, 'RESET_HOLD_MS': 30, 'RESET_RECOVERY_MS': 30,
    'SET_ADDR_RECOVERY_MS': 10, 'RESUME_HOLD_MS': 30,
    'RESUME_RECOVERY_MS': 20, 'SUSPEND_ENTRY_MS': 20,
}
ABI_PROFILE_SHA256 = 'c880590cd8c5ba64f091d5bef7b2499353755bca582f15533509c638e8efb544'
ABI_PROFILE = {
    'freertos_cores': 2, 'tick_hz': 1000, 'notification_slots': 1,
    'use_16_bit_ticks': 0, 'use_task_notifications': 1, 'use_mutexes': 1,
    'dynamic_allocation': 1, 'unicore': 0, 'single_core_mode': 0, 'smp_kernel': 0,
    'base_type_size': 4, 'ubase_type_size': 4, 'tick_type_size': 4,
    'task_handle_size': 4, 'queue_handle_size': 4, 'stack_type_size': 1,
    'stack_alignment': 16, 'port_max_delay': 0xffffffff,
    'portmux_size': 8, 'portmux_alignment': 4, 'portmux_owner_offset': 0,
    'portmux_count_offset': 4, 'portmux_free_value': 0xB33FFFFF,
    'log_config_size': 4, 'log_config_alignment': 4, 'log_config_data_offset': 0,
    'log_version': 1, 'log_default_level': 3, 'log_maximum_level': 3,
    'log_timestamp_rtos': 1, 'internal_cache_level': 0, 'external_cache_level': 1,
    'internal_memory_cached': 0,
}
LOADER_MAP = {
    'relocations_examined': 1078,
    'unmapped_relocations': [], 'unmapped_relative_values': [],
    'unmapped_executable_sections': [],
    'absolute_peripheral_relocations': [
        {'offset': '0x3ab8', 'address': '0x60039000'},
        {'offset': '0x3bb4', 'address': '0x60008000'},
        {'offset': '0x3c28', 'address': '0x60080400'},
        {'offset': '0x468c', 'address': '0x60004000'},
        {'offset': '0x475c', 'address': '0x60080000'},
        {'offset': '0x47ec', 'address': '0x600c0000'},
        {'offset': '0x47f4', 'address': '0x60038000'},
    ],
}
MAPPED_SECTIONS = frozenset(('.text', '.data', '.rodata', '.data.rel.ro', '.bss'))


def relative_path(value):
    if not isinstance(value, str) or len(value.encode('utf-8')) > 192 or not re.fullmatch(
            r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*', value):
        raise ValueError('The selected ELF path must be a bounded relative native path')
    if any(part in ('.', '..') for part in value.split('/')) or not value.endswith('.elf'):
        raise ValueError('The selected ELF path must name a relative .elf without traversal')
    basename = value.rsplit('/', 1)[-1]
    if len(basename) > 127 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*\.elf', basename) or '..' in basename:
        raise ValueError('The selected basename must satisfy the native executable identity contract')
    return value


def _mapped_address(elf, address, size, executable=False):
    return any(section.name in MAPPED_SECTIONS and section['sh_flags'] & 2
               and (not executable or section['sh_flags'] & 4)
               and section['sh_addr'] <= address
               and address + size <= section['sh_addr'] + section['sh_size']
               for section in elf.iter_sections())


def _inspect_elf(data):
    """Structural check; callers must first establish the pinned bytes."""
    elf = ELFFile(io.BytesIO(data))
    if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_XTENSA' or elf['e_type'] != 'ET_DYN':
        raise ValueError('Controller must be the pinned ELF32/Xtensa shared image')
    tables = [elf.get_section_by_name(name) for name in ('.dynsym', '.symtab')]
    if any(table is None for table in tables):
        raise ValueError('Both controller symbol tables are required')
    for table in tables:
        imports = tuple(sorted({symbol.name for symbol in table.iter_symbols()
                                if symbol.name and symbol['st_shndx'] == 'SHN_UNDEF'}))
        if imports != IMPORTS:
            raise ValueError('Controller does not have the exact pinned 44-import inventory')
    exports = [symbol for symbol in tables[0].iter_symbols()
               if symbol.name and symbol['st_shndx'] != 'SHN_UNDEF'
               and symbol['st_info']['bind'] in ('STB_GLOBAL', 'STB_WEAK')]
    if (len(exports) != 1 or exports[0].name != 't5_driver_get'
            or exports[0]['st_info']['bind'] != 'STB_GLOBAL'
            or exports[0]['st_info']['type'] != 'STT_FUNC'
            or not _mapped_address(elf, exports[0]['st_value'], max(exports[0]['st_size'], 1), True)):
        raise ValueError('One mapped dynamic global provider entry is required')
    descriptors = [symbol for symbol in tables[1].iter_symbols()
                   if symbol.name == '_ZN12_GLOBAL__N_1L10hid_driverE'
                   and symbol['st_shndx'] != 'SHN_UNDEF']
    if (len(descriptors) != 1 or descriptors[0]['st_size'] != PROVIDER_SIZE
            or descriptors[0]['st_info']['type'] != 'STT_OBJECT'
            or not _mapped_address(elf, descriptors[0]['st_value'], PROVIDER_SIZE)):
        raise ValueError('Pinned public provider descriptor is absent or has changed')
    fields = struct.unpack('<10I', address_bytes(elf, descriptors[0]['st_value'], PROVIDER_SIZE))
    abi, size, driver, capability, api, interface, *callbacks = fields
    if (abi, size, text_at(elf, driver), text_at(elf, capability), api) != (
            PROVIDER_ABI, PROVIDER_SIZE, DRIVER_ID, CAPABILITY, 1):
        raise ValueError('Pinned public provider descriptor identity does not match')
    if (not interface or not _mapped_address(elf, interface, 8)
            or any(not callback or not _mapped_address(elf, callback, 1, True) for callback in callbacks)):
        raise ValueError('Provider interface or lifecycle/diagnostic callbacks are not mapped')
    return list(IMPORTS)


def inspect_controller(data):
    if len(data) != PINNED_SIZE or hashlib.sha256(data).hexdigest() != PINNED_SHA256:
        raise ValueError('Controller differs from the pinned native-IDF6 image; no policy generated')
    return _inspect_elf(data)


def read_controller(path):
    with path.open('rb') as stream:
        data = stream.read(PINNED_SIZE + 1)
    if len(data) != PINNED_SIZE:
        raise ValueError('Controller length differs from the pinned native-IDF6 image')
    return data


def _exact(actual, expected):
    """Do not accept JSON booleans/floats as integer ABI or API evidence."""
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(_exact(actual[k], v) for k, v in expected.items())
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(_exact(a, b) for a, b in zip(actual, expected))
    return actual == expected


def inspect_qualification(evidence):
    if not isinstance(evidence, dict):
        raise ValueError('Qualification evidence must be an object')
    expected = {
        'schema_version': 1, 'status': 'experimental-native-candidate-not-hardware-qualified',
        'driver_id': DRIVER_ID, 'version': VERSION, 'file_name': 'driver.elf',
        'architecture': 'xtensa-esp32s3', 'target': 'esp32s3',
        'provider_abi': PROVIDER_ABI, 'os_cpu_abi': OS_CPU_ABI,
        'native_cohort': NATIVE_COHORT, 'idf_commit': IDF_COMMIT,
        'usb_commit': USB_COMMIT, 'usb_version': '1.6.0',
        'compiler': '15.2.0', 'compiler_distribution': 'esp-15.2.0_20251204',
        'optimization': '-Os', 'abi_profile': ABI_PROFILE, 'abi_profile_sha256': ABI_PROFILE_SHA256,
        'requires': REQUIREMENTS, 'provides': [{'capability': CAPABILITY, 'api': 1}],
        'size': PINNED_SIZE, 'sha256': PINNED_SHA256, 'imports': list(IMPORTS),
        'usb_config': USB_CONFIG,
    }
    for key, value in expected.items():
        if not _exact(evidence.get(key), value):
            raise ValueError('Qualification evidence differs from pinned ' + key)
    hashes = evidence.get('source_hashes')
    if (not isinstance(hashes, dict) or not hashes
            or any(not isinstance(path, str) or not path or len(path) > 4096
                   or not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest)
                   for path, digest in hashes.items())):
        raise ValueError('Qualification source hashes are absent or malformed')
    required_hashes = {
        '/Drivers/usb_controller_esp32s3/idf61-imports.txt': IMPORT_FILE_SHA256,
    }
    for key, suffix in (('sdkconfig_sha256', '/config/sdkconfig.h'),
                        ('compile_commands_sha256', '/compile_commands.json')):
        digest = evidence.get(key)
        if not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest):
            raise ValueError('Qualification lacks explicit ' + key)
        required_hashes[suffix] = digest
    for suffix, digest in required_hashes.items():
        matches = [value for path, value in hashes.items() if path.endswith(suffix)]
        if matches != [digest]:
            raise ValueError('Qualification source hash mismatch or ambiguity: ' + suffix)
    if not _exact(evidence.get('loader_map'), LOADER_MAP):
        raise ValueError('Qualification differs from the pinned successful loader-map audit')


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate qualification JSON field: ' + key)
        result[key] = value
    return result


def read_qualification(path):
    with path.open('rb') as stream:
        data = stream.read(1024 * 1024 + 1)
    if len(data) > 1024 * 1024:
        raise ValueError('Qualification evidence exceeds the 1 MiB bound')
    return json.loads(data, object_pairs_hook=_unique_object)


def render(data, selected_path, qualification):
    selected_path = relative_path(selected_path)
    imports = inspect_controller(data)
    inspect_qualification(qualification)
    names = '\n'.join(f'    "{name}",' for name in imports)
    digest = ','.join('0x' + PINNED_SHA256[i:i + 2] for i in range(0, 64, 2))
    return f'''#pragma once
// Experimental native-IDF6 {DRIVER_ID}@{VERSION}; SHA-256 {PINNED_SHA256}.
// Cohort: {NATIVE_COHORT}. OS/CPU ABI {OS_CPU_ABI}; diagnostic ABI {DIAGNOSTIC_ABI}.
// ABI-relevant profile SHA-256: {ABI_PROFILE_SHA256}; optimization: -Os.
// Public provider descriptor ABI {PROVIDER_ABI}/{PROVIDER_SIZE} bytes is independently versioned.
// ESP-IDF {IDF_COMMIT}; esp-usb {USB_COMMIT}.
// Trusted firmware input only. Sidecars/manifest/boot JSON cannot select this file.
// Generation is not controller activation or physical hardware qualification.
#include "runtime/drivers/NativeProviderPolicyV1.h"
namespace RuntimeProviders {{
inline NativeProviderPolicySetV1 selectedNativeProviderPoliciesV1() {{
  static const char* const imports[]={{
{names}
  }};
  static const NativeProviderRequirementV1 requirements[]={{{{"board.power.vbus",1}}}};
  static const NativeProviderPolicyV1 policy{{
    "{selected_path}","{DRIVER_ID}","{VERSION}","{CAPABILITY}",1,{OS_CPU_ABI},{PINNED_SIZE},
    {{{digest}}},imports,{len(imports)},requirements,1,{DIAGNOSTIC_ABI}
  }};
  return {{&policy,1}};
}}
}} // namespace RuntimeProviders
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--controller', type=Path, required=True)
    parser.add_argument('--qualification', type=Path, required=True)
    parser.add_argument('--relative-elf-path', required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--output', type=Path)
    mode.add_argument('--check', type=Path)
    args = parser.parse_args()
    try:
        if args.output and args.output.resolve() in (args.controller.resolve(), args.qualification.resolve()):
            raise ValueError('The generated header must not replace a controller or qualification input')
        expected = render(read_controller(args.controller), args.relative_elf_path,
                          read_qualification(args.qualification)).encode('utf-8')
        if args.check:
            if args.check.read_bytes() != expected:
                raise ValueError('Generated policy differs from the exact trusted policy')
            print('Exact native-IDF6 controller policy check PASS')
        else:
            args.output.write_bytes(expected)
            print('Generated exact 44-import ABI-2 trusted policy; no build selection or activation')
    except (ValueError, OSError, ELFError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
