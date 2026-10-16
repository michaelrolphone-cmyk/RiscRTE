#!/usr/bin/env python3
"""Transfer individual files through an explicitly admitted RTEF1 endpoint.

No device discovery, reset, flashing, recursive copy, deletion or formatting.
The endpoint is optional and disabled in ordinary builds. See docs/SERIAL_FILES.md.
"""
import argparse
from dataclasses import dataclass
import math
import os
from pathlib import Path
import re
import secrets
import select
import stat
import sys
import time
import zlib

CHUNK = 48
LINE = 192
PATH_MAX = 511
UINT64_MAX = (1 << 64) - 1
STATUS = {1: 'not found', 2: 'end', 3: 'already exists', -1: 'invalid request',
          -2: 'I/O error', -3: 'service unavailable', -4: 'busy',
          -5: 'denied or read-only', -6: 'retained custody; restart required',
          -7: 'wrong owner context', -9: 'no space', -10: 'sequence mismatch',
          -11: 'integrity mismatch'}


class FileTransferError(RuntimeError):
    """A failed transfer; never interpreted as successful or automatically retried."""


class ProtocolError(FileTransferError):
    pass


class OutcomeUnknown(FileTransferError):
    pass


class RemoteError(FileTransferError):
    def __init__(self, command, code, fields=()):
        self.command, self.code, self.fields = command, code, tuple(fields)
        super().__init__(f'{command}: {STATUS.get(code, "unknown status")} ({code})')


@dataclass(frozen=True)
class Namespace:
    id: int
    writable: bool
    label: str


@dataclass(frozen=True)
class FileInfo:
    flags: int
    size: int

    @property
    def directory(self):
        return bool(self.flags & 1)


def unsigned(value, name, maximum=UINT64_MAX):
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise ValueError(f'{name} must be an unsigned integer <= {maximum}')
    return value


def path_bytes(path):
    if not isinstance(path, str):
        raise ValueError('namespace path must be a string')
    try:
        encoded = path.encode('utf-8', errors='strict')
    except UnicodeError as error:
        raise ValueError('namespace path must be valid UTF-8') from error
    if (not path.startswith('/') or path == '/' or len(encoded) > PATH_MAX
            or any(c < 32 or c == 127 or c == 92 for c in encoded)
            or any(part in ('', '.', '..') for part in path[1:].split('/'))):
        raise ValueError('path must be absolute UTF-8, <= 511 bytes, with no empty, '
                         'dot, parent, control or backslash components; root is not a file')
    return encoded


class FDTransport:
    """Bounded, deadline-driven byte transport; also used by pipe-only host tests."""
    def __init__(self, read_fd, write_fd):
        self.read_fd, self.write_fd = read_fd, write_fd
        self._buffer = b''
        self._at = 0

    @staticmethod
    def _remaining(deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('serial response deadline expired')
        return remaining

    def write(self, data, deadline):
        view = memoryview(data)
        while view:
            remaining = self._remaining(deadline)
            if not select.select([], [self.write_fd], [], remaining)[1]:
                raise TimeoutError('serial write deadline expired')
            try:
                actual = os.write(self.write_fd, view[:512])
            except (BlockingIOError, InterruptedError):
                continue
            if actual <= 0:
                raise EOFError('serial connection closed during write')
            view = view[actual:]

    def line(self, deadline):
        # Keep at most 191 content bytes, even for unbounded diagnostic output.
        line = bytearray()
        overflow = False
        while True:
            remaining = self._remaining(deadline)
            if self._at == len(self._buffer):
                if not select.select([self.read_fd], [], [], remaining)[0]:
                    raise TimeoutError('serial response deadline expired')
                try:
                    self._buffer = os.read(self.read_fd, 512)
                except (BlockingIOError, InterruptedError):
                    continue
                self._at = 0
                if not self._buffer:
                    raise EOFError('serial connection closed before a complete response')
            value = self._buffer[self._at]
            self._at += 1
            if value == 10:
                if overflow:
                    if line.startswith(b'RTEF1'):
                        raise ProtocolError('oversize protocol response')
                    return b''  # Ignore a complete oversized diagnostic line.
                return bytes(line)
            if len(line) < LINE - 1:
                line.append(value)
            else:
                overflow = True


class SerialTransport(FDTransport):
    def __init__(self, device):
        import termios
        if not stat.S_ISCHR(os.stat(device).st_mode):
            raise ValueError('an explicit serial character device is required')
        fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.previous = None
        try:
            if not stat.S_ISCHR(os.fstat(fd).st_mode):
                raise ValueError('selected path is not a serial character device')
            self.previous = termios.tcgetattr(fd)
            settings = termios.tcgetattr(fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = 0
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, settings)
        except BaseException:
            os.close(fd)
            raise
        super().__init__(fd, fd)

    def close(self):
        import termios
        if self.read_fd is not None:
            fd = self.read_fd
            self.read_fd = self.write_fd = None
            try:
                termios.tcsetattr(fd, termios.TCSANOW, self.previous)
            finally:
                os.close(fd)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Client:
    def __init__(self, transport, timeout=5.0, session=None):
        if not math.isfinite(timeout) or not 0 < timeout < 30:
            raise ValueError('per-request timeout must be positive and below 30 seconds')
        self.transport, self.timeout = transport, timeout
        self.session = secrets.randbelow(0xffffffff) + 1 if session is None else session
        unsigned(self.session, 'session', 0xffffffff)
        if not self.session:
            raise ValueError('session must be nonzero')
        self.sequence, self.usable = 0, True
        fields = self._request('HELLO')
        self._expect(fields, 'HELLO', 3)
        chunk = self._number(fields[1], 'chunk size')
        path_max = self._number(fields[2], 'path size')
        self.namespace_count = self._number(fields[3], 'namespace count', 16)
        if chunk != CHUNK or path_max != PATH_MAX:
            self._bad('unsupported endpoint bounds')

    def _bad(self, message):
        self.usable = False
        raise ProtocolError(message + '; outcome unconfirmed; session unusable; no automatic retry')

    def _number(self, token, name, maximum=UINT64_MAX):
        if not re.fullmatch(r'[0-9]+', token) or int(token) > maximum:
            self._bad('invalid ' + name + ' in response')
        return int(token)

    def _expect(self, fields, command, arguments):
        if len(fields) != arguments + 1 or fields[0] != command:
            self._bad('invalid ' + command + ' response')

    def _request(self, command, *arguments):
        if not self.usable or self.sequence == 0xffffffff:
            raise ProtocolError('session unusable; reconnect only after inspecting the outcome')
        self.sequence += 1
        prefix = f'RTEF1 {self.session:08x} {self.sequence}'
        frame = (prefix + ' ' + command + ''.join(' ' + str(a) for a in arguments) + '\n').encode('ascii')
        if len(frame) > LINE:
            self._bad('request exceeds protocol line bound')
        deadline = time.monotonic() + self.timeout
        try:
            self.transport.write(frame, deadline)
            while True:
                line = self.transport.line(deadline)
                if not line.startswith(b'RTEF1'):
                    continue  # Boot/runtime diagnostics are not protocol responses.
                if (len(line) >= LINE or any(c < 32 or c > 126 for c in line)):
                    raise ProtocolError('malformed protocol response')
                words = line.decode('ascii').split(' ')
                if (len(words) < 4 or any(not word for word in words)
                        or ' '.join(words[:3]) != prefix
                        or not re.fullmatch(r'-?[0-9]+', words[3])):
                    raise ProtocolError('malformed or mismatched session/sequence response')
                code = int(words[3])
                if not -(1 << 31) <= code < (1 << 31):
                    raise ProtocolError('invalid response status')
                if code:
                    if code in (-6, -10):
                        self.usable = False
                    raise RemoteError(command, code, words[4:])
                return words[4:]
        except RemoteError:
            raise
        except (Exception, KeyboardInterrupt) as error:
            self.usable = False
            raise OutcomeUnknown(f'{command} outcome unknown: {error}; no automatic retry, '
                                 'reset or reconnect performed') from error

    def namespaces(self):
        result = []
        for index in range(self.namespace_count):
            fields = self._request('NS', index)
            self._expect(fields, 'NS', 3)
            nsid = self._number(fields[1], 'namespace ID')
            writable = self._number(fields[2], 'writable flag', 1)
            if not nsid or nsid in {item.id for item in result}:
                self._bad('zero or duplicate namespace ID')
            if not re.fullmatch(r'(?:[0-9a-f]{2}){1,63}', fields[3]):
                self._bad('invalid namespace label encoding')
            try:
                label = bytes.fromhex(fields[3]).decode('utf-8', errors='strict')
            except UnicodeError:
                self._bad('invalid UTF-8 namespace label')
            if any(ord(c) < 32 or ord(c) == 127 for c in label):
                self._bad('control character in namespace label')
            result.append(Namespace(nsid, bool(writable), label))
        return result

    def _path(self, namespace, path):
        unsigned(namespace, 'namespace ID')
        if not namespace:
            raise ValueError('namespace ID must be nonzero')
        encoded = path_bytes(path)
        for offset in range(0, len(encoded), CHUNK):
            block = encoded[offset:offset + CHUNK]
            fields = self._request('PATH', offset, block.hex())
            self._expect(fields, 'PATH', 1)
            if self._number(fields[1], 'path offset') != offset + len(block):
                self._bad('wrong path offset')

    def stat(self, namespace, path):
        self._path(namespace, path)
        fields = self._request('STAT', namespace)
        self._expect(fields, 'STAT', 2)
        return FileInfo(self._number(fields[1], 'stat flags', 0xffffffff),
                        self._number(fields[2], 'file size'))

    def mkdir(self, namespace, path):
        self._path(namespace, path)
        fields = self._request('MKDIR', namespace)
        self._expect(fields, 'MKDIR', 0)

    def abort(self):
        fields = self._request('ABORT')
        self._expect(fields, 'ABORT', 0)

    def _cleanup(self):
        if not self.usable:
            return 'remote handle outcome unknown; no cleanup request sent'
        try:
            self.abort()
            return 'remote handle closed; selected file was not removed or restored'
        except FileTransferError as error:
            return f'remote cleanup not confirmed: {error}'

    def put_stream(self, namespace, path, source, size, replace=False):
        unsigned(size, 'source size')
        self._path(namespace, path)
        try:
            fields = self._request('PUT', namespace, 'replace' if replace else 'create', size)
            self._expect(fields, 'PUT', 0)
            offset, crc = 0, 0
            while offset < size:
                block = source.read(min(CHUNK, size - offset))
                if not isinstance(block, bytes) or not 0 < len(block) <= min(CHUNK, size - offset):
                    raise FileTransferError('source ended early or returned invalid data')
                used = 0
                while used < len(block):
                    fields = self._request('WRITE', offset, block[used:].hex())
                    self._expect(fields, 'WRITE', 1)
                    actual = self._number(fields[1], 'write offset')
                    if not offset < actual <= offset + len(block) - used:
                        self._bad('wrong or non-progressing write offset')
                    count = actual - offset
                    crc = zlib.crc32(block[used:used + count], crc)
                    used += count
                    offset = actual
            if source.read(1) != b'':
                raise FileTransferError('source grew beyond its declared size')
            fields = self._request('COMMIT', f'{crc:08x}')
            self._expect(fields, 'COMMIT', 0)
            return size
        except (Exception, KeyboardInterrupt) as error:
            cleanup = self._cleanup()
            raise FileTransferError(f'upload not confirmed: {error}; {cleanup}. '
                                    'A selected destination may be truncated or partial; '
                                    'inspect it before any new transfer.') from error

    def get_stream(self, namespace, path, sink):
        self._path(namespace, path)
        try:
            fields = self._request('GET', namespace)
            self._expect(fields, 'GET', 1)
            size = self._number(fields[1], 'file size')
            offset, crc = 0, 0
            while offset < size:
                fields = self._request('READ', offset)
                self._expect(fields, 'READ', 2)
                actual = self._number(fields[1], 'read offset')
                if not re.fullmatch(r'(?:[0-9a-f]{2}){1,48}', fields[2]):
                    self._bad('invalid read data')
                block = bytes.fromhex(fields[2])
                if actual != offset + len(block) or actual > size:
                    self._bad('wrong read offset')
                used = 0
                while used < len(block):
                    count = sink.write(block[used:])
                    if isinstance(count, bool) or not isinstance(count, int) or not 0 < count <= len(block) - used:
                        raise FileTransferError('local destination made no progress or returned an invalid count')
                    used += count
                crc = zlib.crc32(block, crc)
                offset = actual
            fields = self._request('CLOSE')
            self._expect(fields, 'CLOSE', 1)
            if not re.fullmatch(r'[0-9a-f]{8}', fields[1]) or int(fields[1], 16) != crc:
                self._bad('download CRC mismatch')
            return size
        except (Exception, KeyboardInterrupt) as error:
            cleanup = self._cleanup()
            raise FileTransferError(f'download not confirmed: {error}; {cleanup}. '
                                    'Local destination may be partial; do not use it as a verified copy.') from error


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument('--device', required=True, help='explicit serial device path (115200 baud)')
    result.add_argument('--timeout', type=float, default=5.0, help='per-request deadline in seconds, below 30 (default: 5)')
    commands = result.add_subparsers(dest='command', required=True)
    commands.add_parser('namespaces', help='list only firmware-admitted namespace IDs')
    for name in ('put', 'get', 'stat', 'mkdir'):
        command = commands.add_parser(name)
        command.add_argument('--namespace', required=True, type=int, help='explicit numeric namespace ID')
        command.add_argument('--path', required=True, help='absolute path inside the namespace')
        if name in ('put', 'get'):
            command.add_argument('local', type=Path, help='local source (put) or destination (get)')
            command.add_argument('--replace', action='store_true',
                                 help='put: truncate existing remote file; get: truncate local destination')
    return result


def open_source(path):
    # Reject special files before open; nonblocking open and fstat also defend
    # against a FIFO replacement between the path check and descriptor check.
    if not stat.S_ISREG(os.stat(path).st_mode):
        raise ValueError('local upload source must be a regular file')
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError('local upload source must be a regular file')
        return os.fdopen(fd, 'rb')
    except BaseException:
        os.close(fd)
        raise


def open_destination(path, replace=False):
    # Do not follow a final symlink, open a FIFO/device for transfer, or truncate
    # until the opened descriptor has been verified as a regular file.
    try:
        mode = os.lstat(path).st_mode
    except FileNotFoundError:
        mode = None
    if mode is not None and not stat.S_ISREG(mode):
        raise ValueError('local download destination must be a regular file, not a symlink or device')
    flags = os.O_WRONLY | os.O_CREAT | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0)
    if not replace:
        flags |= os.O_EXCL
    fd = os.open(path, flags, 0o600)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError('local download destination must be a regular file')
        if replace:
            os.ftruncate(fd, 0)
        return os.fdopen(fd, 'wb')
    except BaseException:
        os.close(fd)
        raise


def main(argv=None):
    args = parser().parse_args(argv)
    if not math.isfinite(args.timeout) or not 0 < args.timeout < 30:
        raise ValueError('--timeout must be positive and below 30 seconds')
    if args.command != 'namespaces':
        path_bytes(args.path)
        unsigned(args.namespace, 'namespace ID')
        if args.namespace == 0:
            raise ValueError('namespace ID must be nonzero')
    # Validate/open local inputs before touching the explicitly selected device.
    source = None
    if args.command == 'put':
        source = open_source(args.local)
    try:
        with SerialTransport(args.device) as transport:
            client = Client(transport, args.timeout)
            if args.command == 'namespaces':
                for item in client.namespaces():
                    print(f'{item.id}\t{"read-write" if item.writable else "read-only"}\t{item.label}')
            elif args.command == 'stat':
                info = client.stat(args.namespace, args.path)
                print(f'{"directory" if info.directory else "file"}\t{info.size}\tflags={info.flags}')
            elif args.command == 'mkdir':
                client.mkdir(args.namespace, args.path)
                print('Directory created.')
            elif args.command == 'put':
                size = os.fstat(source.fileno()).st_size
                client.put_stream(args.namespace, args.path, source, size, args.replace)
                print(f'Uploaded and committed {size} bytes; CRC32 verified.')
            else:
                # Exclusive creation is the default. No temporary-file rename or
                # replacement is hidden behind a successful-looking download.
                with open_destination(args.local, args.replace) as sink:
                    size = client.get_stream(args.namespace, args.path, sink)
                    sink.flush()
                    os.fsync(sink.fileno())
                print(f'Downloaded {size} bytes; CRC32 verified and local file synced.')
    finally:
        if source is not None:
            source.close()


if __name__ == '__main__':
    try:
        main()
    except (Exception, KeyboardInterrupt) as error:
        print(f'serial_files: {error}', file=sys.stderr)
        sys.exit(1)
