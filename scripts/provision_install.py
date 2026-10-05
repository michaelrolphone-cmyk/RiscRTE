#!/usr/bin/env python3
"""Explicit owner serial installer. No discovery, reset, flashing or erase.

Only the separately built maintenance image implements this protocol. Normal
Runtime exposes no writable endpoint. Tests inject transport; never use devices.
"""
import argparse,hashlib,os,re,select,stat,subprocess,tempfile,time
from pathlib import Path

class SerialTransport:
    def __init__(self,path):
        import termios
        if not stat.S_ISCHR(os.stat(path).st_mode):raise ValueError('explicit serial device required')
        self.fd=os.open(path,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        try:
            self.previous=termios.tcgetattr(self.fd);settings=termios.tcgetattr(self.fd)
            settings[0]=settings[1]=settings[3]=0
            settings[2]=termios.CS8|termios.CREAD|termios.CLOCAL
            settings[4]=settings[5]=termios.B115200
            settings[6][termios.VMIN]=0;settings[6][termios.VTIME]=0
            termios.tcsetattr(self.fd,termios.TCSANOW,settings)
        except BaseException:os.close(self.fd);raise
    def close(self):
        import termios
        try:termios.tcsetattr(self.fd,termios.TCSANOW,self.previous)
        finally:os.close(self.fd)
    def write(self,data,deadline):
        while data:
            remaining=deadline-time.monotonic()
            if remaining<=0 or not select.select([], [self.fd], [], remaining)[1]:raise TimeoutError()
            n=os.write(self.fd,data[:512]);data=data[n:]
    def line(self,deadline):
        data=bytearray()
        while len(data)<192:
            remaining=deadline-time.monotonic()
            if remaining<=0 or not select.select([self.fd], [], [], remaining)[0]:raise TimeoutError()
            value=os.read(self.fd,1)
            if value==b'\n':return bytes(data).rstrip(b'\r')
            data.extend(value)
        raise ValueError('oversize response')

def transfer(transport,source,profile,time_input):
    if not re.fullmatch('[0-9a-f]{40}',source):raise ValueError('exact source SHA required')
    if not 0<len(profile)<=16384 or len(time_input)>384:raise ValueError('input bounds')
    deadline=time.monotonic()+30
    transport.write(b'HELLO\n',deadline)
    handshake=None
    for _ in range(8):
        line=transport.line(deadline)
        if line.startswith(b'RTE_MAINTENANCE_V1 '):handshake=line;break
    expected=b'RTE_MAINTENANCE_V1 '+source.encode()+b' '
    if handshake is None or not handshake.startswith(expected) or not re.fullmatch(b'[0-9a-f]{8}',handshake[len(expected):]):raise ValueError('maintenance source mismatch')
    nonce=handshake[len(expected):];payload=profile+time_input
    header=b'INSTALL '+nonce+b' '+str(len(profile)).encode()+b' '+str(len(time_input)).encode()+b' '+hashlib.sha256(payload).hexdigest().encode()+b'\n'
    # After sending INSTALL any transport failure is ambiguous. Never retry,
    # reset or reconnect automatically; the owner must inspect selected state.
    try:
        transport.write(header,deadline);transport.write(payload,deadline)
        result=transport.line(deadline)
    except Exception as error:raise RuntimeError('installation outcome unknown; no automatic retry') from error
    if result not in (b'RTE_INSTALL INSTALLED',b'RTE_INSTALL UNCHANGED'):
        raise RuntimeError('installation rejected or uncertain; inspect state before retry')
    return result.decode()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validator',required=True)
    parser.add_argument('--profile',type=Path,required=True)
    parser.add_argument('--port',required=True)
    parser.add_argument('--expected-source',required=True)
    parser.add_argument('--time-server')
    args=parser.parse_args()
    # Validate locally before opening any port; credentials are never arguments.
    with tempfile.TemporaryDirectory(prefix='rte-owner-') as temporary:
        output=Path(temporary)/'input'
        command=[str(args.validator),str(args.profile),str(output)]
        if args.time_server:command.append(args.time_server)
        subprocess.run(command,check=True,capture_output=True)
        profile=(output/'profile.bin').read_bytes()
        time_input=(output/'time.bin').read_bytes() if (output/'time.bin').exists() else b''
        if not re.fullmatch('[0-9a-f]{40}',args.expected_source):raise ValueError('exact source SHA required')
        transport=SerialTransport(args.port)
        try:print(transfer(transport,args.expected_source,profile,time_input))
        finally:transport.close()
if __name__=='__main__':
    try:main()
    except Exception:
        raise SystemExit('Owner installation failed or outcome unknown; no retry, reset or erase performed.')
