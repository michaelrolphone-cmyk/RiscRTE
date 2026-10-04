"""Compile actual adapter/app sources; baseline must fail, --proposed must pass."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
system,utilities=map(Path,sys.argv[1:3]);proposed='--proposed' in sys.argv[3:]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'lib/PortableApps/src').mkdir(parents=True);(p/'Apps').mkdir()
 (p/'lib/PortableApps/src/adapter.c').write_bytes((system/'lib/PortableApps/src/adapter.c').read_bytes())
 (p/'Apps/battery.c').write_bytes((utilities/'Apps/battery.c').read_bytes())
 if proposed:
  for patch in ['system-battery-unknown.patch','utilities-battery-unknown.patch']:
   subprocess.run(['patch','-s','-d',str(p),'-p1','-i',str(root/'test/proposals'/patch)],check=True)
 cases={
 'adapter':r'''
#include "lib/PortableApps/src/adapter.c"
#include <assert.h>
const t5_app_manifest_t portable_catalog[1]={{0}};
const unsigned portable_catalog_count=0;
const risc_runtime_api_v1 *risc_runtime_get_api(uint32_t v){(void)v;return NULL;}
static risc_battery_sample_v1 sample;
static bool sample_ok=true;
static bool read_sample(void*c,risc_battery_sample_v1*out){(void)c;*out=sample;return sample_ok;}
int main(void){
 const risc_battery_gauge_api_v1 api={1,sizeof(api),NULL,read_sample};gauge=&api;
 t5_battery_state_t out;
 sample=(risc_battery_sample_v1){4000,255,RISC_BATTERY_PROFILE_MISSING|RISC_BATTERY_CHARGING};
 assert(read_battery(&out));assert(out.available && out.gauge_voltage_mv==4000 && out.charging && out.soc_percent>100);
 sample=(risc_battery_sample_v1){3900,0,0};assert(read_battery(&out) && out.soc_percent==0);
 sample.percent=73;assert(read_battery(&out) && out.soc_percent==73);
 sample.flags=RISC_BATTERY_PROFILE_MISSING;assert(read_battery(&out) && out.soc_percent>100);
 sample_ok=false;assert(!read_battery(&out));
}
''',
 'app':r'''
#include "Apps/battery.c"
#include <assert.h>
const t5_app_api_v1*t5_app_get_api(uint32_t v){(void)v;return NULL;}
const t5_ui_api_v1*t5_ui_get_api(uint32_t v){(void)v;return NULL;}
const t5_battery_api_v1*t5_battery_get_api(uint32_t v){(void)v;return NULL;}
int main(void){t5_battery_state_t s={0};s.available=s.gauge_read_ok=1;s.gauge_voltage_mv=4000;s.soc_percent=UINT16_MAX;
 build_rows(&s);assert(!strcmp(rows[1].value,"Unknown"));assert(!strcmp(rows[2].value,"4000 mV"));
 s.soc_percent=0;build_rows(&s);assert(!strcmp(rows[1].value,"0 %"));
 s.soc_percent=73;build_rows(&s);assert(!strcmp(rows[1].value,"73 %"));}
'''}
 for name,code in cases.items():
  f=p/(name+'.c');f.write_text(code)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(system/'lib/PortableApps/include'),'-I'+str(system/'lib/NativeApps/include'),str(f),'-o',str(p/name)],check=True)
  result=subprocess.run([str(p/name)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  assert (result.returncode==0)==proposed,(name,result.stderr.decode())
  print(name, 'corrected semantics PASS' if proposed else 'baseline defect reproduced')
