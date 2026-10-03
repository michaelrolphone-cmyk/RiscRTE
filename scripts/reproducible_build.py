"""Stable source epoch/path mapping; no wall-clock version stamps or device I/O."""
import subprocess
Import('env')
root=env.subst('$PROJECT_DIR')
epoch=subprocess.check_output(['git','show','-s','--format=%ct','HEAD'],cwd=root,text=True).strip()
env['ENV']['SOURCE_DATE_EPOCH']=epoch
env.Append(CCFLAGS=['-ffile-prefix-map='+root+'=.'])
