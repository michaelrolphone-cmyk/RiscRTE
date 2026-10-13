"""Run the mandatory alias/reservation proof after linking the opt-in target."""
import sys
from pathlib import Path
Import('env')
sys.path.insert(0,str(Path(env.subst('$PROJECT_DIR'))/'scripts'))
from radio_iq_proof import prove
import json
def verify(source,target,env):
 path=Path(str(target[0]))
 # Use the final resolved compiler definitions, after build_unflags filtering.
 # Macro presence matches PairedBank.h's explicit #if defined selection.
 expanded=any((item[0] if isinstance(item,(tuple,list)) else item)=='RISC_PAIRED_APP_DATA_EXPANDED'
              for item in env.get('CPPDEFINES',()))
 proof=prove(path.read_bytes(),expanded=expanded)
 (path.parent/'radio-iq-proof.json').write_text(json.dumps(proof,indent=2,sort_keys=True)+'\n')
 print('Verified IQ bank reservation and DRAM/IRAM alias exclusion:',hex(proof['bank_base']))
env.AddPostAction('$BUILD_DIR/${PROGNAME}.elf',verify)
