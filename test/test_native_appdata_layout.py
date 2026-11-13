#!/usr/bin/env python3
"""Compile product geometry independently; verify exact mount/fence agreement."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=pathlib.Path(tmp);source=p/'check.cpp'
 source.write_text('''#include "ports/esp32s3/NativeAppDataLayout.h"
#include <cassert>
int main(){using namespace RiscNativeAppDataLayout;assert(exact(Offset,Bytes));assert(!exact(Offset+4096,Bytes));assert(overlaps(Offset,Bytes));assert(!overlaps(Offset-4096,4096));assert(!overlaps(Offset+Bytes,4096));assert(overlaps(Offset-4096,8192));assert(overlaps(Offset+Bytes-4096,8192));assert(!overlaps(Offset,0));assert(overlaps(Offset-4096,UINT64_MAX));assert(!overlaps(UINT64_MAX-4095,8192));}''')
 for offset,size,flash in [(0x270000,0x80000,0x1000000),(0x1f00000,0x100000,0x2000000),(0x400000,0x200000,0x800000),(0x10000,0x10000,0x400000)]:
  cmd=['c++','-std=c++17','-I'+str(root/'src'),f'-DRISC_NATIVE_APP_DATA_OFFSET={offset}',f'-DRISC_NATIVE_APP_DATA_BYTES={size}',f'-DRISC_NATIVE_FLASH_BYTES={flash}',str(source),'-o',str(p/'check')]
  subprocess.run(cmd,check=True);subprocess.run([str(p/'check')],check=True)
 for offset,size,flash in [(0x270001,0x80000,0x1000000),(0,4096,0x1000000),(0x1f00000,0x200000,0x2000000),(0x270000,0,0x1000000),(0x10000,0xffffffffffff0000,0x1000000)]:
  r=subprocess.run(['c++','-std=c++17','-I'+str(root/'src'),f'-DRISC_NATIVE_APP_DATA_OFFSET={offset}',f'-DRISC_NATIVE_APP_DATA_BYTES={size}',f'-DRISC_NATIVE_FLASH_BYTES={flash}',str(source),'-o',str(p/'invalid')],capture_output=True)
  assert r.returncode
 for name in ['NativeAppData.cpp','NativeAppDataMount.cpp']:
  s=(root/'src/ports/esp32s3'/name).read_text();assert 'RiscNativeAppDataLayout::exact' in s;assert 'address!=0x270000' not in s
print('PASS product AppData geometry: default/round/arbitrary exact fences, overlap edges and rejecting invalid geometry')
