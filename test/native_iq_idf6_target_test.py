#!/usr/bin/env python3
"""Compile production IQ lifecycle against the pinned SDK; no firmware/hardware claim."""
import argparse
import importlib.util
import json
import shlex
import struct
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile

PIN = 'fff9895c82d744c7237be8847347bdd1b07c6643'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands', type=Path, required=True)
    parser.add_argument('--idf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rom-elf', type=Path, required=True)
    args = parser.parse_args()
    idf = args.idf.resolve()
    assert subprocess.check_output(['git', '-C', str(idf), 'rev-parse', 'HEAD'], text=True).strip() == PIN
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    entries = [e for e in json.loads(args.compile_commands.read_text()) if e['file'].endswith('/NativeHardware.cpp')]
    assert len(entries) == 1
    entry = entries[0]
    source = out / 'native-iq.cpp'
    source.write_text('''#include <esp_idf_version.h>
#include "ports/esp32s3/NativeRadioIq.h"
static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(6,1,0));
#if !RISC_NATIVE_ESP_IDF || !CONFIG_IDF_TARGET_ESP32S3 || !CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND
#error Native IQ requires the pinned S3 SDK and its PSRAM atomic workaround
#endif
extern "C" bool iq_ready(){return RiscCpu::NativeRadioIq::ready();}
extern "C" bool iq_prepare(){return RiscCpu::NativeRadioIq::prepare();}
extern "C" bool iq_cleanup(){return RiscCpu::NativeRadioIq::cleanup();}
extern "C" bool iq_start(uint32_t interval,risc_radio_iq_tick_v1 tick,void* arg){return RiscCpu::NativeRadioIq::workerStart(interval,tick,arg);}
extern "C" bool iq_stop(){return RiscCpu::NativeRadioIq::workerStop();}
extern "C" uint64_t iq_now(){return RiscCpu::NativeRadioIq::nowUs();}
''')
    flags = Path(entry['directory']) / 'toolchain/cxxflags'
    assert '-mdisable-hardware-atomics' in flags.read_text()
    spec = importlib.util.spec_from_file_location('radio_iq_proof', Path(__file__).resolve().parents[1] / 'scripts/radio_iq_proof.py')
    proof = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(proof)
    rom = proof.prove_rom(args.rom_elf.read_bytes())
    tools = json.loads((idf / 'tools/tools.json').read_text())['tools']
    versions = next(tool for tool in tools if tool['name'] == 'esp-rom-elfs')['versions']
    assert any(v['name'] == '20241011' and v['status'] == 'recommended' and
               v['any']['sha256'] == '921f000164a421c7628fbfee55b173384aafaa51883adc65cd27bf9b0af9e9a9'
               for v in versions)
    report = {'sdk_commit': PIN, 'rom': rom, 'objects': {}, 'linked_firmware_verified': False, 'hardware_verified': False}
    for diagnostics in (0, 1):
        name = 'native-iq-' + str(diagnostics)
        command = list(entry.get('arguments') or shlex.split(entry['command']))
        obj = out / (name + '.o')
        command[command.index('-o') + 1] = str(obj)
        command[command.index('-c') + 1] = str(source)
        for flag, extension in (('-MF', '.d'), ('-MT', '.o')):
            if flag in command:
                command[command.index(flag) + 1] = str(out / (name + extension))
        command += ['-URISC_SLEEP_DIAGNOSTICS', '-DRISC_SLEEP_DIAGNOSTICS=' + str(diagnostics)]
        result = subprocess.run(command, cwd=entry['directory'], text=True, capture_output=True)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        (out / (name + '-command.json')).write_text(json.dumps(command, indent=2) + '\n')
        assert result.returncode == 0, result.stdout + result.stderr
        with obj.open('rb') as stream:
            elf = ELFFile(stream)
            assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_XTENSA'
            reserve = elf.get_section_by_name('.reserved_memory_address')
            assert reserve and reserve.data() == struct.pack('<II', 0x3FCB0000, 0x3FCC0000)
            symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
            imports = {s.name for s in symbols if s['st_shndx'] == 'SHN_UNDEF'}
            expected = {'esp_phy_enable', 'esp_phy_disable', 'esp_phy_modem_init', 'esp_phy_modem_deinit',
                        'esp_wifi_bt_power_domain_on', 'esp_wifi_bt_power_domain_off',
                        'esp_timer_stop_blocking', 'esp_timer_delete', 'esp_timer_get_time', 'xQueueSemaphoreTake',
                        'xTaskGetTickCount', 'vTaskDelete'}
            assert expected <= imports, expected - imports
            assert 'esp_timer_stop' not in imports
            assert len([s for s in symbols if 'reserved_region_risc_radio_iq' in s.name]) == 1
            report['objects'][obj.name] = {'reservation': [0x3FCB0000, 0x3FCC0000], 'imports': sorted(imports)}
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Native IQ: pinned SDK ESP32-S3 headers, PHY/timer API, reservation object, exact ROM stubs and PSRAM workaround flags PASS (no final link or device execution)')


if __name__ == '__main__':
    main()
