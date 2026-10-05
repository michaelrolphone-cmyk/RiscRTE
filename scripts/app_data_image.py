#!/usr/bin/env python3
"""Create an offline EMPTY LittleFS first-install image from the exact core pin.

Never opens a serial port, formats a device, changes a partition table or writes
an existing output. This is NOT a data-preserving update/migration utility.
Prefer --littlefs-source for target-matching disk2.1 and real core remount proof.
An optional verified official mklittlefs tests older-format interoperability.
Native ESP adapter/hardware qualification remains separate.
"""
import argparse,hashlib,json,pathlib,re,subprocess,tempfile
BYTES=0x80000
LAYOUT='riscrte-paired-appdata-v2'
def build(tool,expected,output):
 tool=pathlib.Path(tool).resolve(strict=True);output=pathlib.Path(output)
 if not re.fullmatch('[0-9a-f]{64}',expected) or hashlib.sha256(tool.read_bytes()).hexdigest()!=expected:raise ValueError('mklittlefs custody mismatch')
 if output.exists() or output.is_symlink():raise ValueError('output already exists; no overwrite')
 with tempfile.TemporaryDirectory() as temporary:
  temporary=pathlib.Path(temporary);empty=temporary/'empty';empty.mkdir();image=temporary/'appdata.bin'
  subprocess.run([str(tool),'-c',str(empty),'-b','4096','-p','256','-s',str(BYTES),str(image)],check=True)
  payload=image.read_bytes()
  if len(payload)!=BYTES or b'littlefs' not in payload[:8192]:raise ValueError('invalid initial LittleFS image geometry/header')
  # Tool-level round trip is evidence of an empty image, not native mount proof.
  unpack=temporary/'unpacked';unpack.mkdir()
  subprocess.run([str(tool),'-u',str(unpack),'-b','4096','-p','256','-s',str(BYTES),str(image)],check=True)
  if list(unpack.iterdir()):raise ValueError('initial app-data image is not empty')
  output.mkdir(parents=True,exist_ok=False)
  (output/'appdata.bin').write_bytes(payload)
  record={'schema':1,'kind':'littlefs-interoperability-fixture','initial_provisioning_image':False,'disk_version':'2.0' if expected=='f04600f5f02e7c851ffb20749b35e26eea155c0f437c4c20d33de60fbeb999a6' else 'unverified','size_bytes':BYTES,'sha256':hashlib.sha256(payload).hexdigest(),'tool_sha256':expected,'block_size':4096,'page_size':256,'contents':'empty','tool_round_trip_verified':True,'native_mount_verified':False,'scope':'Interoperability fixture only; not an initial provisioning or deployment image. Older LittleFS disk formats may upgrade during a write. No device action.'}
  (output/'appdata-image.json').write_text(json.dumps(record,sort_keys=True,indent=2)+'\n')
 return record
PIN='f53a0cc961a8acac85f868b431d2f3e58e447ba3'
FILES={'lfs.c':'0b9845f350c33aa448a916847d2246b76c8e296eed8e7e296ae0430242cfda6c','lfs.h':'a8c8d70f0863fbbc46ce17c5dc7673b40f1b3c9e7e10f3bf33fd28f03dc67703','lfs_util.c':'f2fbde533670560434bd9f5a547174cc7c5a4670a02c47b4bd85180dced8b2ec','lfs_util.h':'03e912a6e9894c9d10c61f5da22b89ebe0bb778af67972d7b67a5f160731bf72'}
PROVISION_SCOPE='Initial provisioning only. Exact target LittleFS core/configuration; ESP adapter and physical flash qualification separate. Installation overwrites app-data; never include in a routine data-preserving update. No device or migration action.'
INITIAL_IMAGE_SHA256='5f03c248f2de31c4da9ae8d9bc2033df064cee5e37694a9982f70fbb2f1d2ef0'
def verify_initial(directory):
 directory=pathlib.Path(directory);image=directory/'appdata.bin';manifest=directory/'appdata-image.json'
 if image.is_symlink() or manifest.is_symlink() or image.stat().st_size!=BYTES or manifest.stat().st_size>16384:raise ValueError('initial image custody/bounds')
 def unique(pairs):
  value={}
  for key,item in pairs:
   if key in value:raise ValueError('duplicate initial image field')
   value[key]=item
  return value
 record=json.loads(manifest.read_text(),object_pairs_hook=unique)
 required={'schema':1,'kind':'initial-app-data-image','layout':LAYOUT,'partition':'appdata','offset':0x270000,'size_bytes':BYTES,'sha256':INITIAL_IMAGE_SHA256,'littlefs_commit':PIN,'source_sha256':FILES,'block_size':4096,'read_size':128,'prog_size':128,'cache_size':512,'disk_version':'2.1','contents':'empty'}
 if set(record)!=set(required)|{'generator_sha256','initial_provisioning_image','core_mount_verified','native_mount_verified','scope'}:raise ValueError('unexpected initial image evidence fields')
 if any(type(record.get(k)) is not type(v) or record[k]!=v for k,v in required.items()) or record.get('initial_provisioning_image') is not True or record.get('core_mount_verified') is not True or record.get('native_mount_verified') is not False or record.get('scope')!=PROVISION_SCOPE:raise ValueError('not a verified software-only target disk2.1 initial image')
 generator=pathlib.Path(__file__).resolve().with_name('app_data_format.c')
 if record.get('generator_sha256')!=hashlib.sha256(generator.read_bytes()).hexdigest():raise ValueError('initial image generator differs from source')
 if hashlib.sha256(image.read_bytes()).hexdigest()!=INITIAL_IMAGE_SHA256:raise ValueError('initial image bytes differ from reproduced core image')
 return record
def build_from_source(source,output):
 source=pathlib.Path(source).resolve(strict=True);output=pathlib.Path(output)
 if output.exists() or output.is_symlink():raise ValueError('output already exists; no overwrite')
 if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()!=PIN:raise ValueError('LittleFS source commit mismatch')
 for name,digest in FILES.items():
  if hashlib.sha256((source/name).read_bytes()).hexdigest()!=digest:raise ValueError('LittleFS source bytes mismatch: '+name)
 with tempfile.TemporaryDirectory() as temporary:
  temporary=pathlib.Path(temporary);tool=temporary/'format';image=temporary/'appdata.bin'
  generator=pathlib.Path(__file__).resolve().with_name('app_data_format.c')
  subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-I'+str(source),str(generator),str(source/'lfs.c'),str(source/'lfs_util.c'),'-o',str(tool)],check=True)
  subprocess.run([str(tool),str(image)],check=True)
  payload=image.read_bytes()
  if len(payload)!=BYTES:raise ValueError('invalid initial image length')
  record={'schema':1,'kind':'initial-app-data-image','initial_provisioning_image':True,'layout':LAYOUT,'partition':'appdata','offset':0x270000,'size_bytes':BYTES,'sha256':hashlib.sha256(payload).hexdigest(),'littlefs_commit':PIN,'source_sha256':FILES,'generator_sha256':hashlib.sha256(generator.read_bytes()).hexdigest(),'block_size':4096,'read_size':128,'prog_size':128,'cache_size':512,'disk_version':'2.1','contents':'empty','core_mount_verified':True,'native_mount_verified':False,'scope':PROVISION_SCOPE}
  output.mkdir(parents=True,exist_ok=False);(output/'appdata.bin').write_bytes(payload);(output/'appdata-image.json').write_text(json.dumps(record,sort_keys=True,indent=2)+'\n')
 verify_initial(output)
 return record
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);group=p.add_mutually_exclusive_group(required=True);group.add_argument('--littlefs-source',type=pathlib.Path,help='Preferred: exact pinned LittleFS source checkout');group.add_argument('--mklittlefs',type=pathlib.Path,help='Optional older-format interoperability fixture');p.add_argument('--tool-sha256');p.add_argument('--output',required=True,type=pathlib.Path);a=p.parse_args()
 if a.littlefs_source:record=build_from_source(a.littlefs_source,a.output)
 else:
  if not a.tool_sha256:p.error('--mklittlefs requires --tool-sha256')
  record=build(a.mklittlefs,a.tool_sha256,a.output)
 print(json.dumps(record,indent=2))
