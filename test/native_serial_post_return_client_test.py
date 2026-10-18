#!/usr/bin/env python3
"""Actual scripts.serial_files.Client over child pipes, never a serial device."""
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('serial_files',ROOT/'scripts/serial_files.py')
serial=importlib.util.module_from_spec(spec);sys.modules[spec.name]=serial;spec.loader.exec_module(serial)

class TraceTransport(serial.FDTransport):
    def __init__(self,read_fd,write_fd):
        super().__init__(read_fd,write_fd);self.longest=0;self.requests=0
    def write(self,data,deadline):
        self.started=time.monotonic();self.requests+=1
        print('TX '+data.decode().rstrip(),flush=True)
        return super().write(data,deadline)
    def line(self,deadline):
        line=super().line(deadline)
        print('RX '+line.decode(),flush=True)
        if hasattr(self,'started'):self.longest=max(self.longest,time.monotonic()-self.started)
        return line

def run(executable,root):
    process=subprocess.Popen([str(executable),str(root),'pipe'],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
    transport=TraceTransport(process.stdout.fileno(),process.stdin.fileno())
    try:
        assert transport.line(time.monotonic()+5)==b'READY'
        client=serial.Client(transport,session=0xabcdef12)
        assert client.timeout==5.0 and [(n.id,n.label) for n in client.namespaces()]==[(2,'Serial files'),(12,'SD card')]
        assert client.stat(12,'/unrelated.txt').size==12
        assert client.stat(2,'/unrelated.txt').size==7
        for ns in (2,12):
            data=bytes((i*37+ns)%256 for i in range(521))
            assert client.put_stream(ns,'/client.bin',io.BytesIO(data),len(data))==len(data)
            assert client.stat(ns,'/client.bin').size==len(data)
            sink=io.BytesIO();assert client.get_stream(ns,'/client.bin',sink)==len(data) and sink.getvalue()==data
            try:client.put_stream(ns,'/client.bin',io.BytesIO(b'x'),1)
            except serial.FileTransferError as e:assert 'already exists' in str(e)
            else:raise AssertionError('create guard bypassed')
            try:client.put_stream(ns,'/missing.bin',io.BytesIO(b'x'),1,replace=True)
            except serial.FileTransferError as e:assert 'not found' in str(e)
            else:raise AssertionError('replace guard bypassed')
            replacement=b'private replacement' if ns==2 else b'SD replacement'
            assert client.put_stream(ns,'/client.bin',io.BytesIO(replacement),len(replacement),replace=True)==len(replacement)
            sink=io.BytesIO();assert client.get_stream(ns,'/client.bin',sink)==len(replacement) and sink.getvalue()==replacement
        for ns,expected in ((2,b'private replacement'),(12,b'SD replacement')):
            sink=io.BytesIO();client.get_stream(ns,'/client.bin',sink);assert sink.getvalue()==expected
        process.stdin.close();process.stdin=None
        remainder=process.stdout.read().decode();print(remainder,end='',flush=True)
        assert process.wait(timeout=10)==0
        assert transport.longest<5.0
        print(f'Actual Python Client post-return pipe qualification PASS: {transport.requests} requests, default deadline=5s, maximum response={transport.longest:.6f}s',flush=True)
        return dict(requests=transport.requests,default_deadline_seconds=client.timeout,max_response_seconds=transport.longest)
    finally:
        if process.poll() is None:process.kill();process.wait()

if __name__=='__main__':run(Path(sys.argv[1]),Path(sys.argv[2]))
