"""Unit tests for the Python janus client and the CRC32 reference corpus.

The CRC32 vectors below are the canonical reference; `tests/transfer_test.cpp`
and `tests/janus_ps1_tests.ps1` pin the C++ and PowerShell implementations
against the same values, and `tests/crc_cross_check.py` additionally
compiles and runs the firmware's crc32() against them end-to-end.
"""
import importlib.util
from pathlib import Path
import unittest
import zlib

spec = importlib.util.spec_from_file_location('janus', Path(__file__).parents[1] / 'tools/janus.py')
janus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(janus)

# (name, payload, expected crc32). Keep this list in lockstep with
# tests/transfer_test.cpp and tests/janus_ps1_tests.ps1.
CRC_VECTORS = [
    ('empty',     b'',                                             0x00000000),
    ('a',         b'a',                                            0xe8b7be43),
    ('abc',       b'abc',                                          0x352441c2),
    ('123456789', b'123456789',                                    0xcbf43926),
    ('fox',       b'The quick brown fox jumps over the lazy dog',  0x414fa339),
    ('ff256',     b'\xff' * 256,                                   0xfea8a821),
    ('range256',  bytes(range(256)),                               0x29058c73),
]


class Stub:
    def __init__(self, responses): self.responses = iter(responses)
    def write(self, data): return len(data)
    def flush(self): pass
    def readline(self, size): return next(self.responses)


class CrcVectorTest(unittest.TestCase):
    def test_reference_vectors_agree_with_zlib(self):
        for name, data, expected in CRC_VECTORS:
            with self.subTest(name=name):
                self.assertEqual(zlib.crc32(data) & 0xffffffff, expected)


class ClientTest(unittest.TestCase):
    def test_download(self):
        data = b'hello\r\n'
        device = janus.Device(Stub([
            f'FILE {len(data)} {zlib.crc32(data)}\n'.encode(),
            b'DATA ' + data.hex().encode() + b'\n',
        ]))
        self.assertEqual(device.get(), data)

    def test_corruption_single_attempt(self):
        device = janus.Device(Stub([b'FILE 1 0\n', b'DATA 61\n']))
        with self.assertRaisesRegex(RuntimeError, 'CRC'):
            device.get(max_attempts=1)

    def test_corruption_exhausts_retries(self):
        # Same garbage three times -> CRC mismatch after retries.
        responses = [b'FILE 1 0\n', b'DATA 61\n'] * 3
        device = janus.Device(Stub(responses))
        with self.assertRaisesRegex(RuntimeError, 'CRC mismatch after retries'):
            device.get(max_attempts=3)

    def test_timeout(self):
        with self.assertRaisesRegex(RuntimeError, 'Timed out'):
            janus.Device(Stub([b''])).request('HELLO')

    def test_upload_limit(self):
        with self.assertRaises(ValueError):
            janus.Device(Stub([])).put(b'a' * 65537)

    def test_empty_upload(self):
        janus.Device(Stub([b'OK BEGIN\n', b'OK COMMIT\n'])).put(b'')

    def test_upload_retries_on_err_crc(self):
        # First COMMIT fails ERR CRC, second succeeds.
        responses = [
            b'OK BEGIN\n', b'OK DATA 1\n', b'ERR CRC\n',
            b'OK BEGIN\n', b'OK DATA 1\n', b'OK COMMIT\n',
        ]
        janus.Device(Stub(responses)).put(b'a', max_attempts=2)

    def test_upload_err_size_does_not_retry(self):
        # ERR SIZE is unrecoverable; the client must surface it on the first attempt.
        with self.assertRaisesRegex(RuntimeError, 'ERR SIZE'):
            janus.Device(Stub([b'ERR SIZE\n'])).put(b'a', max_attempts=3)


if __name__ == '__main__':
    unittest.main()
