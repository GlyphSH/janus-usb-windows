"""Windows USB serial file roundtrip. Explicit paths, no automatic host collection."""
import argparse
from pathlib import Path
import zlib

LIMIT = 65536

class Device:
    def __init__(self, port):
        self.port = port

    def request(self, command):
        self.port.write((command + '\n').encode('ascii'))
        self.port.flush()
        answer = self.port.readline(1024)
        if not answer.endswith(b'\n'):
            raise RuntimeError('Timed out or incomplete response; reconnect and retry the transfer')
        answer = answer.decode('ascii').rstrip('\r\n')
        if answer.startswith('ERR'):
            raise RuntimeError(answer)
        return answer

    def expect(self, command, expected):
        answer = self.request(command)
        if answer != expected:
            raise RuntimeError(f'Unexpected response: {answer!r}')

    def put(self, data, max_attempts=3):
        if len(data) > LIMIT:
            raise ValueError('File exceeds 65536 bytes')
        crc = zlib.crc32(data)
        # Retry only on ERR CRC from COMMIT; a failed commit clears the
        # device's receiving_ flag, so re-sending starts a fresh transfer.
        for attempt in range(1, max_attempts + 1):
            self.expect(f'BEGIN {len(data)} {crc}', 'OK BEGIN')
            for offset in range(0, len(data), 256):
                chunk = data[offset:offset + 256]
                self.expect(f'DATA {offset} {chunk.hex()}', f'OK DATA {offset + len(chunk)}')
            try:
                self.expect('COMMIT', 'OK COMMIT')
                return
            except RuntimeError as e:
                if 'ERR CRC' not in str(e) or attempt >= max_attempts:
                    raise

    def sdinfo(self):
        self.port.write(b'SDINFO\n')
        self.port.flush()
        answer = self.port.readline(1024)
        if not answer.endswith(b'\n'):
            return None
        return answer.decode('ascii').rstrip('\r\n')

    def get(self, max_attempts=3):
        for attempt in range(1, max_attempts + 1):
            fields = self.request('INFO').split()
            if len(fields) != 3 or fields[0] != 'FILE':
                raise RuntimeError('Invalid file metadata')
            size, checksum = map(int, fields[1:])
            if not 0 <= size <= LIMIT:
                raise RuntimeError('Invalid file size')
            result = bytearray()
            for offset in range(0, size, 256):
                count = min(256, size - offset)
                reply = self.request(f'READ {offset} {count}')
                if not reply.startswith('DATA '):
                    raise RuntimeError('Invalid chunk response')
                chunk = bytes.fromhex(reply[5:])
                if len(chunk) != count:
                    raise RuntimeError('Invalid chunk length')
                result.extend(chunk)
            if zlib.crc32(result) == checksum:
                return bytes(result)
            if attempt >= max_attempts:
                raise RuntimeError('CRC mismatch after retries')

def _parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM1')
    parser.add_argument('action', choices=['ports', 'put', 'get', 'roundtrip'])
    parser.add_argument('source', nargs='?', type=Path)
    parser.add_argument('destination', nargs='?', type=Path)
    args = parser.parse_args()
    if args.action == 'ports':
        return parser, args
    if args.source is None:
        parser.error('A file path is required')
    if args.action == 'roundtrip' and args.destination is None:
        parser.error('roundtrip requires input and output paths')
    return parser, args


def _list_ports():
    from serial.tools import list_ports
    for port in list_ports.comports():
        print(f'{port.device}: {port.description}')


def _upload(device, source):
    if source.stat().st_size > LIMIT:
        raise ValueError("File exceeds 65536 bytes")
    original = source.read_bytes()
    device.put(original)
    reply = device.sdinfo()
    if reply and reply.startswith('SD '):
        print(f'SD write: {reply[3:]}')
    elif reply == 'ERR SD':
        print('SD not written (no card or write failed)')
    return original


def _download(device, output, original):
    received = device.get()
    if original is not None and received != original:
        raise RuntimeError('Roundtrip byte comparison failed')
    with output.open('xb') as stream:
        stream.write(received)


def main():
    parser, args = _parse_args()
    if args.action == 'ports':
        _list_ports()
        return
    # Exclusive creation protects existing files, including the input of a roundtrip.
    output = args.destination if args.action == 'roundtrip' else args.source
    if args.action != 'put' and output.exists():
        parser.error(f'Output already exists: {output}')
    import serial
    with serial.Serial(args.port, 115200, timeout=5, write_timeout=5) as port:
        port.reset_input_buffer()
        device = Device(port)
        device.expect('HELLO', 'JANUS 1 65536')
        device.expect('CLIENT windows', 'OK CLIENT')
        original = _upload(device, args.source) if args.action in ('put', 'roundtrip') else None
        if args.action in ('get', 'roundtrip'):
            _download(device, output, original)
        print('Transfer verified successfully')

if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error)) from error
