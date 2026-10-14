from pathlib import Path
import subprocess, argparse
parser=argparse.ArgumentParser(description='Compile opt-in native package storage against installed ESP32-S3 SDK; no firmware or hardware qualification.')
parser.add_argument('--packages',type=Path,default=Path.home()/'.platformio/packages')
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args()
r=Path(__file__).resolve().parents[1];packages=a.packages;sdk=packages/'framework-arduinoespressif32/tools/sdk/esp32s3';cxx=packages/'toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++';out=a.output;out.mkdir(parents=True,exist_ok=True)
includes=[r,r/'src',r/'sdk/app',r/'sdk/driver',r/'sdk/hardware',r/'lib/ArduinoJson/src',r/'lib/elf_loader/include',sdk/'qio_opi/include',sdk/'include/freertos/include/esp_additions',sdk/'include/freertos/include/esp_additions/freertos',sdk/'include/freertos/port/xtensa/include/freertos']
for p in (sdk/'include').rglob('*'):
 if p.is_dir() and (p.name in ('include','platform_include') or str(p).endswith(('/include/esp32s3','/include/xtensa','/port/xtensa/include'))):includes.append(p)
flags=['-std=gnu++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Os','-mlongcalls','-fno-exceptions','-fno-rtti','-fstack-usage','-DESP_PLATFORM','-DRISC_PAIRED_APP_DATA','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_PACKAGE_VOLUME=1']
for name in ('ports/esp32s3/NativeAppDataMount','ports/esp32s3/NativeAppData','ports/esp32s3/NativePackageVolume','runtime/storage/PackageFiles'):
 subprocess.run([str(cxx),*flags,*['-I'+str(p) for p in includes],'-c',str(r/'src'/(name+'.cpp')),'-o',str(out/(Path(name).name+'.o'))],check=True)
