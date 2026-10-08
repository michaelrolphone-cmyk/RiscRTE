"""Verify the frozen consumer bytes plus append-only Runtime compatibility."""
import hashlib,os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
LEGACY_SHA='6fbfacaa46d532bfef083e8c7dd3bbe36613e557279fdda887d814d53915526b'
def verify(header):
 header=Path(header).resolve()
 assert hashlib.sha256(header.read_bytes()).hexdigest()==LEGACY_SHA,'unreviewed historical SDK header'
 with tempfile.TemporaryDirectory() as temporary:
  subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
    '-DLEGACY_RUNTIME_HEADER="'+str(header)+'"','-I'+str(ROOT/'sdk/app'),'-c',
    str(ROOT/'test/legacy_runtime_header_abi.c'),'-o',str(Path(temporary)/'probe.o')],check=True)
if __name__=='__main__':
 import sys
 verify(sys.argv[1]);print('Frozen legacy Runtime header and every field/size plus four append-only callback suffixes verified')
