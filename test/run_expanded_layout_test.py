#!/usr/bin/env python3
import argparse,subprocess,tempfile,sys
from pathlib import Path
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output;out.mkdir(parents=True,exist_ok=True)
for abi,defs in ((1,[]),(2,['-DRISC_PAIRED_APP_DATA=1']),(3,['-DRISC_PAIRED_APP_DATA=1','-DRISC_PAIRED_APP_DATA_EXPANDED=1'])):
 dest=out/('layout-'+str(abi));subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',*defs,*['-I'+str(r/p) for p in ('src','sdk/app','sdk/driver','lib/ArduinoJson/src')],str(r/'src/runtime/update/PairedBank.cpp'),str(r/'test/expanded_layout_test.cpp'),'-o',str(dest)],check=True);subprocess.run([str(dest)],check=True)
subprocess.run([sys.executable,str(r/'test/expanded_layout_images_test.py')],check=True)
subprocess.run([sys.executable,str(r/'test/paired_bank_images_test.py')],check=True)
