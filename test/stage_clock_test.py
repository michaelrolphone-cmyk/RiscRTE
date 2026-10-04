"""Use the committed deployment selector; do not duplicate its board policy."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
watch, out = map(Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location('deployment', watch/'scripts/build_clock_deployment.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
board = module.selected_board(json.loads((watch/'hardware/sx1262-915-bma423.json').read_text()))
(out/'board.json').write_text(json.dumps(board))
shutil.copy(watch/'apps/clock/manifest.json',out/'default.json')
boot={'board':'board.json','default_app':'default.elf','drivers':[], 'app_capabilities':[
 {'manifest':'default.json','grants':[{'capability':'display.output','api':1,'instance_id':5},
                                   {'capability':'rtc.clock','api':2,'instance_id':8}]}]}
for ident,name in module.DEVICES.items():
 (out/name).mkdir()
 shutil.copy(watch/f'drivers/twatch_{name}/manifest.json',out/name/'manifest.json')
 boot['drivers'].append({'manifest':f'{name}/manifest.json','instance_id':ident})
(out/'boot.json').write_text(json.dumps(boot))
# The runtime header is a byte-pinned contract, never silently substituted.
assert (watch/'sdk/app/RiscRuntimeV1.h').read_bytes()==(Path(__file__).resolve().parents[1]/'sdk/app/RiscRuntimeV1.h').read_bytes()
