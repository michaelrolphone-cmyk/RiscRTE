#!/usr/bin/env python3
"""Python client -> production C++ endpoint -> PosixFiles, over anonymous pipes."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import zlib

WIRE = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('serial_files', REPO / 'scripts/serial_files.py')
sf = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = sf
spec.loader.exec_module(sf)


class Wire(sf.FDTransport):
    def __init__(self, root, diagnostics=False):
        argv = [str(WIRE), str(root)] + (['diagnostics'] if diagnostics else [])
        self.process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, bufsize=0)
        super().__init__(self.process.stdout.fileno(), self.process.stdin.fileno())
        self.frames = []
        self.last_command = None
        self.closed = False

    def write(self, data, deadline):
        self.frames.append(data)
        self.last_command = data.split()[3].decode()
        super().write(data, deadline)

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.process.stdin.close()
        try:
            code = self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise AssertionError('wire endpoint did not shut down')
        finally:
            self.process.stdout.close()
        errors = self.process.stderr.read().decode(errors='replace')
        self.process.stderr.close()
        if code or errors:
            raise AssertionError(f'wire endpoint failed ({code}): {errors}')

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Intercept:
    def __init__(self, wire, command, rewrite):
        self.wire, self.command, self.rewrite = wire, command, rewrite
        self.used = False

    def write(self, data, deadline):
        self.wire.write(data, deadline)

    def line(self, deadline):
        line = self.wire.line(deadline)
        if self.wire.last_command == self.command and not self.used and line.startswith(b'RTEF1 '):
            self.used = True
            return self.rewrite(line)
        return line


def dropped(_):
    raise TimeoutError('injected lost acknowledgment')


def changed_word(index, value):
    def change(line):
        words = line.split(b' ')
        words[index] = value
        return b' '.join(words)
    return change


class ShortSource(io.BytesIO):
    def read(self, n=-1):
        if not 0 <= n <= sf.CHUNK:
            raise AssertionError('source read was not bounded')
        return super().read(min(n, 13))


class ShortSink(io.BytesIO):
    def write(self, block):
        if len(block) > sf.CHUNK:
            raise AssertionError('sink write was not bounded')
        return super().write(block[:3])


class IntegrationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='rte-wire-')
        self.root = Path(self.temporary.name)
        self.wires = []

    def tearDown(self):
        try:
            for wire in self.wires:
                wire.close()
        finally:
            self.temporary.cleanup()

    def wire(self, diagnostics=False):
        result = Wire(self.root, diagnostics)
        self.wires.append(result)
        return result

    def client(self, transport=None):
        return sf.Client(transport or self.wire(), timeout=2, session=0xabc12345)

    def test_namespaces_stat_and_mkdir(self):
        client = self.client()
        namespaces = client.namespaces()
        self.assertEqual(len(namespaces), 7)
        self.assertEqual(namespaces[0], sf.Namespace(1, True, 'fixture'))
        self.assertEqual(namespaces[1], sf.Namespace(2, False, 'read-only fixture'))
        client.mkdir(1, '/folder')
        self.assertTrue(client.stat(1, '/folder').directory)
        with self.assertRaises(sf.RemoteError) as raised:
            client.mkdir(2, '/forbidden')
        self.assertEqual(raised.exception.code, -5)
        self.assertFalse((self.root / 'forbidden').exists())
        with self.assertRaises(sf.RemoteError) as raised:
            client.stat(1, '/missing')
        self.assertEqual(raised.exception.code, 1)
        with self.assertRaises(sf.RemoteError) as raised:
            client.stat(3, '/missing')
        self.assertEqual(raised.exception.code, -3)
        with self.assertRaises(sf.RemoteError) as raised:
            client.stat(999, '/missing')
        self.assertEqual(raised.exception.code, 1)

    def test_roundtrip_binary_empty_and_all_chunk_boundaries(self):
        client = self.client()
        for size in (0, 1, 47, 48, 49, 96, 97, 1025):
            with self.subTest(size=size):
                data = bytes(i % 256 for i in range(size))
                path = '/file-' + str(size)
                self.assertEqual(client.put_stream(1, path, io.BytesIO(data), size), size)
                self.assertEqual((self.root / path[1:]).read_bytes(), data)
                self.assertEqual(client.stat(1, path).size, size)
                sink = io.BytesIO()
                self.assertEqual(client.get_stream(1, path, sink), size)
                self.assertEqual(sink.getvalue(), data)

    def test_streaming_short_provider_and_local_io(self):
        data = bytes(range(256)) * 257
        client = self.client()
        self.assertEqual(client.put_stream(4, '/short', ShortSource(data), len(data)), len(data))
        sink = ShortSink()
        self.assertEqual(client.get_stream(4, '/short', sink), len(data))
        self.assertEqual(sink.getvalue(), data)

    def test_path_fragments_preserve_utf8_and_spaces(self):
        client = self.client()
        # A multi-byte code point deliberately straddles the 48-byte PATH frame.
        path = '/' + 'a' * 46 + '\u00e9 with space'
        self.assertGreater(len(sf.path_bytes(path)), sf.CHUNK)
        client.put_stream(1, path, io.BytesIO(b'yes'), 3)
        self.assertEqual((self.root / path[1:]).read_bytes(), b'yes')
        client.mkdir(1, '/dir')
        client.put_stream(1, '/dir/other', io.BytesIO(b'next'), 4)
        self.assertEqual((self.root / 'dir/other').read_bytes(), b'next')

    def test_create_never_replaces_and_replace_is_explicit(self):
        target = self.root / 'existing'
        target.write_bytes(b'original long file')
        client = self.client()
        with self.assertRaisesRegex(sf.FileTransferError, 'already exists'):
            client.put_stream(1, '/existing', io.BytesIO(b'new'), 3)
        self.assertEqual(target.read_bytes(), b'original long file')
        client.put_stream(1, '/existing', io.BytesIO(b'new'), 3, replace=True)
        self.assertEqual(target.read_bytes(), b'new')
        with self.assertRaisesRegex(sf.FileTransferError, 'not found'):
            client.put_stream(1, '/absent', io.BytesIO(b'new'), 3, replace=True)
        self.assertFalse((self.root / 'absent').exists())

    def test_readonly_and_unavailable_upload_do_not_create(self):
        client = self.client()
        for namespace, description in ((2, 'denied or read-only'), (3, 'service unavailable')):
            with self.assertRaisesRegex(sf.FileTransferError, description):
                client.put_stream(namespace, '/new', io.BytesIO(b'new'), 3)
            self.assertFalse((self.root / 'new').exists())
        (self.root / 'readable').write_bytes(b'read-only is readable')
        sink = io.BytesIO()
        client.get_stream(2, '/readable', sink)
        self.assertEqual(sink.getvalue(), b'read-only is readable')

    def test_write_failure_preserves_partial_selected_file(self):
        client = self.client()
        with self.assertRaisesRegex(sf.FileTransferError, 'I/O error'):
            client.put_stream(5, '/partial', io.BytesIO(b'abcdefgh'), 8)
        self.assertEqual((self.root / 'partial').read_bytes(), b'abcde')
        self.assertEqual(self.wires[0].last_command, 'ABORT')
        self.assertTrue(client.usable)
        client.put_stream(1, '/after', io.BytesIO(b'ok'), 2)

    def test_zero_write_fails_without_infinite_loop(self):
        client = self.client()
        with self.assertRaisesRegex(sf.FileTransferError, 'I/O error'):
            client.put_stream(7, '/zero', io.BytesIO(b'abc'), 3)
        self.assertEqual((self.root / 'zero').read_bytes(), b'')
        self.assertEqual(sum(b' WRITE ' in f for f in self.wires[0].frames), 1)

    def test_sync_failure_is_not_success(self):
        client = self.client()
        with self.assertRaisesRegex(sf.FileTransferError, 'COMMIT: I/O error'):
            client.put_stream(6, '/unsynced', io.BytesIO(b'abc'), 3)
        self.assertEqual((self.root / 'unsynced').read_bytes(), b'abc')
        self.assertEqual(self.wires[0].last_command, 'ABORT')

    def test_lost_commit_acknowledgment_is_unknown_and_not_retried(self):
        wire = self.wire()
        client = self.client(Intercept(wire, 'COMMIT', dropped))
        with self.assertRaisesRegex(sf.FileTransferError, 'COMMIT outcome unknown'):
            client.put_stream(1, '/committed', io.BytesIO(b'abc'), 3)
        self.assertEqual((self.root / 'committed').read_bytes(), b'abc')
        self.assertFalse(client.usable)
        self.assertEqual(wire.last_command, 'COMMIT')
        self.assertEqual(sum(b' COMMIT ' in f for f in wire.frames), 1)
        before = len(wire.frames)
        with self.assertRaises(sf.ProtocolError):
            client.stat(1, '/committed')
        self.assertEqual(len(wire.frames), before)

    def test_lost_put_acknowledgment_can_leave_truncated_file(self):
        (self.root / 'selected').write_bytes(b'original')
        wire = self.wire()
        client = self.client(Intercept(wire, 'PUT', dropped))
        with self.assertRaisesRegex(sf.FileTransferError, 'PUT outcome unknown'):
            client.put_stream(1, '/selected', io.BytesIO(b'new'), 3, replace=True)
        self.assertEqual((self.root / 'selected').read_bytes(), b'')
        self.assertEqual(wire.last_command, 'PUT')

    def test_wrong_write_offset_stops_before_commit(self):
        wire = self.wire()
        client = self.client(Intercept(wire, 'WRITE', changed_word(5, b'999')))
        with self.assertRaisesRegex(sf.FileTransferError, 'wrong or non-progressing write offset'):
            client.put_stream(1, '/bad-offset', io.BytesIO(b'abcdef'), 6)
        self.assertEqual(wire.last_command, 'WRITE')
        self.assertNotIn(b'COMMIT', b''.join(wire.frames))

    def test_wrong_read_offset_is_rejected(self):
        (self.root / 'source').write_bytes(b'abcdef')
        wire = self.wire()
        client = self.client(Intercept(wire, 'READ', changed_word(5, b'999')))
        sink = io.BytesIO()
        with self.assertRaisesRegex(sf.FileTransferError, 'wrong read offset'):
            client.get_stream(1, '/source', sink)
        self.assertEqual(sink.getvalue(), b'')
        self.assertEqual(wire.last_command, 'READ')

    def test_download_corruption_is_detected_at_crc(self):
        (self.root / 'source').write_bytes(b'abcdef')
        wire = self.wire()
        client = self.client(Intercept(wire, 'READ', changed_word(6, b'626263646566')))
        sink = io.BytesIO()
        with self.assertRaisesRegex(sf.FileTransferError, 'CRC mismatch'):
            client.get_stream(1, '/source', sink)
        self.assertEqual(sink.getvalue(), b'bbcdef')
        self.assertEqual(wire.last_command, 'CLOSE')

    def test_wrong_session_wrong_sequence_and_malformed_success(self):
        for index, value in ((1, b'deadbeef'), (2, b'900'), (4, b'WRONG')):
            with self.subTest(index=index):
                wire = self.wire()
                client = self.client(Intercept(wire, 'MKDIR', changed_word(index, value)))
                with self.assertRaises(sf.FileTransferError):
                    client.mkdir(1, '/directory' + str(index))
                self.assertFalse(client.usable)
                self.assertEqual(wire.last_command, 'MKDIR')
                self.assertTrue((self.root / ('directory' + str(index))).is_dir())

    def test_protocol_oversize_line_is_fatal(self):
        wire = self.wire()
        client = self.client(Intercept(wire, 'STAT', lambda line: line + b'x' * 200))
        (self.root / 'file').write_bytes(b'abc')
        with self.assertRaisesRegex(sf.OutcomeUnknown, 'malformed protocol response'):
            client.stat(1, '/file')

    def test_long_interleaved_diagnostics_do_not_confuse_frames(self):
        client = self.client(self.wire(diagnostics=True))
        data = bytes(range(256))
        client.put_stream(1, '/diagnostics', io.BytesIO(data), len(data))
        sink = io.BytesIO()
        client.get_stream(1, '/diagnostics', sink)
        self.assertEqual(sink.getvalue(), data)

    def test_local_source_failures_abort_but_do_not_restore_destination(self):
        client = self.client()
        for data, size, reason in ((b'a', 2, 'ended early'), (b'ab', 1, 'grew')):
            with self.subTest(reason=reason):
                (self.root / 'selected').write_bytes(b'original')
                with self.assertRaisesRegex(sf.FileTransferError, reason):
                    client.put_stream(1, '/selected', io.BytesIO(data), size, replace=True)
                self.assertEqual((self.root / 'selected').read_bytes(), b'a')
                self.assertEqual(self.wires[0].last_command, 'ABORT')

    def test_retained_status_poison_session_without_cleanup(self):
        wire = self.wire()
        client = self.client(Intercept(wire, 'WRITE', changed_word(3, b'-6')))
        with self.assertRaisesRegex(sf.FileTransferError, 'retained custody; restart required'):
            client.put_stream(1, '/retained', io.BytesIO(b'abc'), 3)
        self.assertFalse(client.usable)
        self.assertEqual(wire.last_command, 'WRITE')
        self.assertEqual((self.root / 'retained').read_bytes(), b'abc')

    def test_download_missing_and_unavailable_are_distinct(self):
        client = self.client()
        for namespace, reason in ((1, 'not found'), (3, 'service unavailable')):
            with self.subTest(namespace=namespace):
                sink = io.BytesIO()
                with self.assertRaisesRegex(sf.FileTransferError, reason):
                    client.get_stream(namespace, '/missing', sink)
                self.assertEqual(sink.getvalue(), b'')
                self.assertTrue(client.usable)

    def test_lost_write_acknowledgment_never_retries_chunk(self):
        wire = self.wire()
        client = self.client(Intercept(wire, 'WRITE', dropped))
        with self.assertRaisesRegex(sf.FileTransferError, 'WRITE outcome unknown'):
            client.put_stream(1, '/partial', io.BytesIO(b'abcdef'), 6)
        self.assertEqual((self.root / 'partial').read_bytes(), b'abcdef')
        self.assertEqual(wire.last_command, 'WRITE')
        self.assertEqual(sum(b' WRITE ' in f for f in wire.frames), 1)

    def test_local_sink_zero_progress_aborts(self):
        class Sink:
            def write(self, _):
                return 0
        (self.root / 'source').write_bytes(b'abc')
        client = self.client()
        with self.assertRaisesRegex(sf.FileTransferError, 'local destination made no progress'):
            client.get_stream(1, '/source', Sink())
        self.assertEqual(self.wires[0].last_command, 'ABORT')

    def test_crc_rejection_on_real_endpoint(self):
        client = self.client()
        client._path(1, '/crc')
        client._request('PUT', 1, 'create', 3)
        client._request('WRITE', 0, b'abc'.hex())
        with self.assertRaises(sf.RemoteError) as raised:
            client._request('COMMIT', '00000000')
        self.assertEqual(raised.exception.code, -11)
        client.abort()
        self.assertEqual((self.root / 'crc').read_bytes(), b'abc')

    def test_exact_duplicate_frame_does_not_repeat_write(self):
        wire = self.wire()
        client = self.client(wire)
        client._path(1, '/duplicate')
        client._request('PUT', 1, 'create', 3)
        response = client._request('WRITE', 0, b'abc'.hex())
        frame = wire.frames[-1]
        wire.write(frame, time.monotonic() + 2)
        duplicate = wire.line(time.monotonic() + 2).decode().split(' ')[4:]
        self.assertEqual(response, duplicate)
        client._request('COMMIT', f'{zlib.crc32(b"abc"):08x}')
        self.assertEqual((self.root / 'duplicate').read_bytes(), b'abc')

    def test_cli_download_exclusive_default_and_explicit_replace(self):
        (self.root / 'remote').write_bytes(b'remote data')
        destination = self.root / 'local'
        destination.write_bytes(b'local original')
        def factory(_):
            return self.wire()
        args = ['--device', '/explicit/mock', 'get', '--namespace', '1', '--path', '/remote', str(destination)]
        with mock.patch.object(sf, 'SerialTransport', factory), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(FileExistsError):
                sf.main(args)
            self.assertEqual(destination.read_bytes(), b'local original')
            sf.main(args + ['--replace'])
        self.assertEqual(destination.read_bytes(), b'remote data')

    def test_cli_upload_download_and_metadata(self):
        source = self.root / 'input.bin'
        source.write_bytes(bytes(range(256)) * 3)
        destination = self.root / 'output.bin'
        output = io.StringIO()
        def factory(_):
            return self.wire()
        base = ['--device', '/explicit/mock']
        with mock.patch.object(sf, 'SerialTransport', factory), contextlib.redirect_stdout(output):
            sf.main(base + ['namespaces'])
            sf.main(base + ['put', '--namespace', '1', '--path', '/remote.bin', str(source)])
            sf.main(base + ['stat', '--namespace', '1', '--path', '/remote.bin'])
            sf.main(base + ['get', '--namespace', '1', '--path', '/remote.bin', str(destination)])
            sf.main(base + ['mkdir', '--namespace', '1', '--path', '/directory'])
        self.assertEqual(destination.read_bytes(), source.read_bytes())
        self.assertIn('Uploaded and committed 768 bytes; CRC32 verified.', output.getvalue())
        self.assertIn('Downloaded 768 bytes; CRC32 verified and local file synced.', output.getvalue())
        self.assertTrue((self.root / 'directory').is_dir())

    def test_invalid_paths_rejected_before_sending(self):
        client = self.client()
        wire = self.wires[0]
        before = len(wire.frames)
        for path in ('/', 'relative', '//x', '/a/', '/a//b', '/.', '/a/../b', '/a\\b',
                     '/a\x00b', '/a\nb', '/a\x7fb', '/' + 'x' * 511, '/\ud800'):
            with self.subTest(path=repr(path)), self.assertRaises(ValueError):
                client.stat(1, path)
        self.assertEqual(len(wire.frames), before)
        self.assertEqual(len(sf.path_bytes('/' + 'x' * 510)), 511)

    def test_local_fifo_and_symlink_destinations_are_rejected(self):
        fifo = self.root / 'fifo'
        os.mkfifo(fifo)
        with self.assertRaises(ValueError):
            sf.open_source(fifo)
        with self.assertRaises(ValueError):
            sf.open_destination(fifo, True)
        target = self.root / 'target'
        target.write_bytes(b'keep')
        link = self.root / 'link'
        link.symlink_to(target)
        with self.assertRaises(ValueError):
            sf.open_destination(link, True)
        self.assertEqual(target.read_bytes(), b'keep')

    def test_cli_invalid_inputs_never_open_device(self):
        with mock.patch.object(sf, 'SerialTransport') as transport:
            for args in (
                    ['--timeout', 'nan', 'namespaces'],
                    ['--timeout', '30', 'namespaces'],
                    ['stat', '--namespace', '0', '--path', '/valid'],
                    ['stat', '--namespace', '1', '--path', '/../bad']):
                with self.assertRaises(ValueError):
                    sf.main(['--device', '/explicit/mock'] + args)
            transport.assert_not_called()


class TransportTest(unittest.TestCase):
    def test_oversize_protocol_frame_and_partial_eof(self):
        for payload, error in ((b'RTEF1 ' + b'x' * 300 + b'\n', sf.ProtocolError),
                               (b'RTEF1 partial', EOFError)):
            with self.subTest(error=error):
                read, write = os.pipe()
                try:
                    os.write(write, payload)
                    os.close(write)
                    write = None
                    with self.assertRaises(error):
                        sf.FDTransport(read, read).line(time.monotonic() + 1)
                finally:
                    os.close(read)
                    if write is not None:
                        os.close(write)

    def test_unterminated_diagnostic_has_a_deadline(self):
        read, write = os.pipe()
        try:
            os.write(write, b'x' * 500)
            start = time.monotonic()
            with self.assertRaises(TimeoutError):
                sf.FDTransport(read, read).line(start + 0.02)
            self.assertLess(time.monotonic() - start, 1)
        finally:
            os.close(read)
            os.close(write)

    def test_serial_transport_rejects_regular_file(self):
        with tempfile.NamedTemporaryFile() as target:
            with self.assertRaisesRegex(ValueError, 'serial character device'):
                sf.SerialTransport(target.name)


if __name__ == '__main__':
    if WIRE is None:
        raise SystemExit('usage: serial_files_client_test.py COMPILED_WIRE_ENDPOINT')
    unittest.main(verbosity=2)
