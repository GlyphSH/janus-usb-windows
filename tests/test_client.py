import importlib.util
from pathlib import Path
import unittest
import zlib

spec = importlib.util.spec_from_file_location('janus', Path(__file__).parents[1] / 'tools/janus.py')
janus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(janus)

class Stub:
    def __init__(self, responses): self.responses = iter(responses)
    def write(self, data): return len(data)
    def flush(self): pass
    def readline(self, size): return next(self.responses)

class ClientTest(unittest.TestCase):
    def test_download(self):
        data = b'hello\r\n'
        device = janus.Device(Stub([f'FILE {len(data)} {zlib.crc32(data)}\n'.encode(), b'DATA ' + data.hex().encode() + b'\n']))
        self.assertEqual(device.get(), data)

    def test_corruption(self):
        device = janus.Device(Stub([b'FILE 1 0\n', b'DATA 61\n']))
        with self.assertRaisesRegex(RuntimeError, 'CRC'): device.get()

    def test_timeout(self):
        with self.assertRaisesRegex(RuntimeError, 'Timed out'):
            janus.Device(Stub([b''])).request('HELLO')

    def test_upload_limit(self):
        with self.assertRaises(ValueError): janus.Device(Stub([])).put(b'a' * 65537)

    def test_empty_upload(self):
        janus.Device(Stub([b'OK BEGIN\n', b'OK COMMIT\n'])).put(b'')

if __name__ == '__main__': unittest.main()
