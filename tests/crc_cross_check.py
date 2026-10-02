#!/usr/bin/env python3
"""Cross-implementation CRC32 equivalence check.

Compiles tests/crc_runner.cpp (which wraps the firmware's crc32()) and
runs it against a canonical byte-sequence corpus, comparing each result
with Python's zlib.crc32. On any disagreement the exit status is non-zero
so CI fails loudly.

The PowerShell implementation is pinned against the same corpus by
tests/janus_ps1_tests.ps1, which runs in the Windows CI job. By
transitivity, agreement here plus agreement there means all three
implementations compute the same CRC32 variant on identical inputs.
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import zlib

VECTORS = [
    ('empty',     b''),
    ('a',         b'a'),
    ('abc',       b'abc'),
    ('123456789', b'123456789'),
    ('fox',       b'The quick brown fox jumps over the lazy dog'),
    ('ff256',     b'\xff' * 256),
    ('range256',  bytes(range(256))),
    ('range256_reversed', bytes(reversed(range(256)))),
    ('crlf_200',  b'\r\n' * 100),
]


def compile_runner(root: pathlib.Path, out_dir: pathlib.Path) -> pathlib.Path:
    binary = out_dir / ('crc_runner.exe' if os.name == 'nt' else 'crc_runner')
    cmd = [
        os.environ.get('CXX', 'c++'),
        '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-I', str(root / 'include'),
        str(root / 'src' / 'transfer.cpp'),
        str(root / 'tests' / 'crc_runner.cpp'),
        '-o', str(binary),
    ]
    subprocess.check_call(cmd)
    return binary


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory() as tmp:
        binary = compile_runner(root, pathlib.Path(tmp))
        failures = 0
        for name, data in VECTORS:
            expected = zlib.crc32(data) & 0xffffffff
            # Pass the payload as a hex string on argv to avoid stdin/encoding
            # issues with raw bytes on Windows runners.
            out = subprocess.check_output([str(binary), data.hex()])
            actual = int(out.decode().strip(), 16)
            if actual != expected:
                print(f'FAIL {name:20s} cpp=0x{actual:08x} zlib=0x{expected:08x}')
                failures += 1
            else:
                print(f'OK   {name:20s} 0x{actual:08x}')
        if failures:
            print(f'{failures} vector(s) disagree between cpp and zlib')
            return 1
    print(f'CRC32 cross-check: {len(VECTORS)} vectors agree (cpp vs zlib.crc32)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
