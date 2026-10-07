"""Stable source epoch/path mapping; no wall-clock version stamps or device I/O."""
import subprocess
Import('env')
root=env.subst('$PROJECT_DIR')
epoch=subprocess.check_output(['git','show','-s','--format=%ct','HEAD'],cwd=root,text=True).strip()
env['ENV']['SOURCE_DATE_EPOCH']=epoch
env.Append(CCFLAGS=['-ffile-prefix-map='+root+'=.'])

# Make the compiled candidate itself carry its exact checkout identity. An old
# incremental output cannot be relabelled by release staging at a newer HEAD.
from pathlib import Path
sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
build=Path(env.subst('$BUILD_DIR'));build.mkdir(parents=True,exist_ok=True)
header=build/'RiscBuildIdentity.h'
version=env.GetProjectConfig().get('riscrte','version')
content='#define RISC_BUILD_SOURCE_SHA "'+sha+'"\n#define RISC_BUILD_IDENTITY "RTE_SOURCE='+sha+'"\n#define RISC_BUILD_VERSION "'+version+'"\n'
if not header.exists() or header.read_text()!=content:header.write_text(content)
env.Append(CPPPATH=[str(build)])
