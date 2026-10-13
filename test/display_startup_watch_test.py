#!/usr/bin/env python3
"""Exercise production Watch panel resume through the real Runtime GPIO hold."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def run(args):
    subprocess.run(list(map(str,args)),check=True)

def main():
    runtime=Path(os.environ['RISCRTE_RUNTIME_ROOT']).resolve()
    watch=Path(os.environ['RISCRTE_WATCH_ROOT']).resolve()
    reader=Path(os.environ['RISCRTE_READER_ROOT']).resolve()
    x4=Path(os.environ['RISCRTE_X4_ROOT']).resolve()
    sys.path.insert(0,str(watch/'scripts'))
    from test_contracts import config,initializer
    from generate_board import generate
    import json
    generate()
    board=json.loads((watch/'hardware/sx1262-915-bma423.json').read_text())
    buses={b['instance_id']:b for b in board['buses']}
    entry=board['devices'][4]
    typ,c=config(entry,buses,False)
    with tempfile.TemporaryDirectory() as tmp:
        out=Path(tmp)
        (out/'fixture_config.h').write_text(f'static {typ} m_config={initializer(c)};\nstatic risc_hardware_device_v1 m_device={{1,sizeof(m_device),{entry["instance_id"]},"{entry["compatible"]}","unspecified","{entry["config_type"]}",1,sizeof(m_config),&m_config}};\n')
        text=(watch/'tests/deep_sleep_driver_test.c').read_text()
        needle='    (void)c;assert(m_tokens[t] && outputs[t] && !active_pwm[t]);'
        assert text.count(needle)==1
        text=text.replace(needle,needle+'\n    const int32_t native_rc=display_test_cpu_hold(t,enable);\n    if(native_rc)return native_rc;',1)
        needle='assert(a->resume_status(NULL)==0 && !deep_held && unhold_at>hold_at && pwm_at>unhold_at);'
        assert text.count(needle)==1
        text=text.replace(needle,'int32_t resume=a->resume_status(NULL);\n        if(getenv("PAD_HOLD_EXPECT_BROKEN")){assert(resume==RISC_LIGHT_SLEEP_PLATFORM && deep_held && !deep_retained);puts("WATCH: production panel resume failure reproduced");return 0;}\n        assert(resume==0 && !deep_held && unhold_at>hold_at && pwm_at>unhold_at);',1)
        text='#include <stdint.h>\n#include <stdbool.h>\n#include <stdlib.h>\nextern int32_t display_test_cpu_hold(uint64_t,bool);\n'+text
        (out/'watch.c').write_text(text)
        san=[]
        if os.environ.get('SANITIZE')=='1':
            san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie','-g']
        run([os.environ.get('CC','cc'),'-std=c11','-g',*san,*['-I'+str(p) for p in (watch/'sdk/driver',watch/'include',out,watch)],'-DTEST_KIND=5',f'-DDRIVER_SOURCE="{watch}/drivers/twatch_panel/driver.c"','-c',out/'watch.c','-o',out/'watch.o'])
        run([sys.executable,x4/'minimal/scripts/prepare_sdk.py','--runtime',runtime,'--reader',reader,'--output',out/'sdk'])
        names=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/ports/esp32s3/CpuPort.cpp']
        includes=[out/'sdk',runtime/'src',runtime/'sdk/app',runtime/'lib/ArduinoJson/src',runtime/'test/drivers/stubs']
        run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-rdynamic',*san,*['-I'+str(p) for p in includes],*[runtime/p for p in names],ROOT/'test/display_startup_watch_bridge.cpp',out/'watch.o','-ldl','-o',out/'test'])
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
        subprocess.run([str(out/'test'),'normal'],env=env,check=True)

if __name__=='__main__':
    main()
