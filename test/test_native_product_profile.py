#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    work=Path(tmp);(work/'parts.csv').write_text('app0,app,ota_0,0x10000,0x260000\n')
    good='set(RISC_NATIVE_TARGET example)\nset(RISC_NATIVE_PARTITIONS "'+str(work/'parts.csv')+'")\n'
    runner=work/'run.cmake'
    runner.write_text('set(RISC_PRODUCT_PROFILE "'+str(work/'profile.cmake')+'")\nset(CMAKE_BINARY_DIR "'+str(work)+'")\ninclude("'+str(ROOT/'idf/product-profile.cmake')+'")\n')
    cases=[('valid',good,None),('missing target',good.replace('set(RISC_NATIVE_TARGET example)\n',''),'must declare RISC_NATIVE_TARGET'),
        ('wrong target',good.replace('example','bad/target'),'Invalid native product identity'),
        ('Arduino',good+'set(RISC_NATIVE_DEFINITIONS ARDUINO_USB_MODE=1)\n','may not change the native SDK cohort'),
        ('SDK override',good+'set(RISC_NATIVE_DEFINITIONS CONFIG_FREERTOS_USE_TRACE_FACILITY=1)\n','may not change the native SDK cohort'),
        ('missing storage wrap',good+'set(RISC_NATIVE_DEFINITIONS RISC_NATIVE_APP_DATA_MOUNT=1)\n','must match RISC_NATIVE_APP_DATA_MOUNT=1'),
        ('missing IQ proof',good+'set(RISC_NATIVE_DEFINITIONS RISC_ENABLE_RADIO_IQ=1)\n','must match RISC_ENABLE_RADIO_IQ=1'),
        ('missing IQ implementation',good+'set(RISC_NATIVE_IQ ON)\n','must match RISC_ENABLE_RADIO_IQ=1'),
        ('valid features',good+'set(RISC_NATIVE_IQ ON)\nset(RISC_NATIVE_APPDATA ON)\nset(RISC_NATIVE_DEFINITIONS RISC_ENABLE_RADIO_IQ=1 RISC_NATIVE_APP_DATA_MOUNT=1)\n',None)]
    for name,content,error in cases:
        (work/'profile.cmake').write_text(content)
        r=subprocess.run(['cmake','-P',str(runner)],capture_output=True,text=True)
        assert (r.returncode!=0)==bool(error),(name,r.stdout,r.stderr)
        if error:assert error in r.stderr,(name,r.stderr)
    print('Native product profile identity/cohort/feature guards:9 cases PASS')
