from pathlib import Path
import subprocess,argparse,configparser
p=argparse.ArgumentParser(description='Compile next-boot request adapter and policy against installed ESP32-S3 SDK; no flashing.')
p.add_argument('--packages',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1];sdk=a.packages/'framework-arduinoespressif32/tools/sdk/esp32s3';cxx=a.packages/'toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++';a.output.mkdir(parents=True,exist_ok=True)
config=configparser.ConfigParser();config.read(r/'platformio.ini');version=config['riscrte']['version'];source=subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip()
(a.output/'RiscBuildIdentity.h').write_text('#pragma once\n#define RISC_BUILD_VERSION \"'+version+'\"\n#define RISC_BUILD_SOURCE_SHA \"'+source+'\"\n#define RISC_BUILD_IDENTITY \"RTE_SOURCE='+source+'\"\n')
includes=[a.output,r,r/'src',r/'sdk/app',r/'sdk/driver',r/'sdk/hardware',r/'lib/ArduinoJson/src',r/'lib/elf_loader/include',sdk/'qio_opi/include',sdk/'include/freertos/include/esp_additions',sdk/'include/freertos/include/esp_additions/freertos',sdk/'include/freertos/port/xtensa/include/freertos',sdk/'include/lwip/include/apps',sdk/'include/lwip/lwip/src/include',sdk/'include/lwip/port/esp32/include',sdk/'include/esp_hw_support/include/soc',a.packages/'framework-arduinoespressif32/cores/esp32',a.packages/'framework-arduinoespressif32/variants/esp32s3']
for d in (sdk/'include').rglob('*'):
 if d.is_dir() and (d.name in ('include','platform_include') or str(d).endswith(('/include/esp32s3','/include/xtensa','/port/xtensa/include'))):includes.append(d)
flags=['-std=gnu++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-Os','-mlongcalls','-fno-exceptions','-fno-rtti','-fstack-usage','-DESP_PLATFORM','-DRISC_PAIRED_BANKS=1','-DRISC_PAIRED_APP_DATA=1','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_PACKAGE_VOLUME=1','-DRISC_NATIVE_PROVISION_REQUEST=1','-DARDUINO_USB_CDC_ON_BOOT=1','-DRISC_TARGET="esp32s3"']
for name in ('ports/esp32s3/NativeProvisionRequest','ports/esp32s3/NativePackageVolumeAdmission','ports/esp32s3/NativeBootstrap','ports/esp32s3/NativeBankStore','runtime/provisioning/RequestQueue','runtime/provisioning/PreservingPolicy','runtime/provisioning/Installer','bootstrap/Runtime','main'):
 subprocess.run([str(cxx),*flags,*['-I'+str(d) for d in includes],'-c',str(r/'src'/(name+'.cpp')),'-o',str(a.output/(Path(name).name+'.o'))],check=True)
print('PASS: target request queue, native bridge, boot adapter, paired-store adapter, inert package volume, input preflight, Runtime graph and policy compiled; no device qualification')
