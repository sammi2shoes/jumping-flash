"""RS05 0.5.0.14 zero-speed CAN-disconnect test; official AT USB-CAN adapter.
Run: py watchdog_disconnect.py COM3
Requires: py -m pip install pyserial
The Stick must run WatchdogObserver and remain on the motor CAN bus.
"""
import math
import struct
import sys
import time

MOTOR, HOST = 1, 126
REPORT = bytes([1, 2, 3, 4, 5, 6, 1, 8])


class Adapter:
    def __init__(self, port):
        self.port = port
        self.buf = bytearray()
        self.last_tx = 0.0

    def send(self, kind, data=bytes(8)):
        if kind not in (3, 4, 17, 18, 24) or len(data) != 8:
            raise ValueError('Unsupported command')
        if kind == 18:
            index = struct.unpack_from('<H', data)[0]
            if index not in (0x7005, 0x700A, 0x7018, 0x7028):
                raise ValueError('Unsupported parameter')
            if index == 0x700A and data[4:] != bytes(4):
                raise ValueError('Only ZERO speed is permitted')
        while time.monotonic() - self.last_tx < .025:
            time.sleep(.001)
        ident = (kind << 24) | (HOST << 8) | MOTOR
        packet = b'AT' + struct.pack('>I', (ident << 3) | 4) + b'\x08' + data + b'\r\n'
        if self.port.write(packet) != len(packet):
            raise RuntimeError('Incomplete serial write')
        self.last_tx = time.monotonic()

    def frames(self):
        self.buf.extend(self.port.read(min(max(self.port.in_waiting, 1), 4096)))
        result = []
        while True:
            start = self.buf.find(b'AT')
            if start < 0:
                self.buf[:] = b'A' if self.buf.endswith(b'A') else b''
                break
            del self.buf[:start]
            if len(self.buf) < 7:
                break
            raw = struct.unpack_from('>I', self.buf, 2)[0]
            size = self.buf[6]
            if size > 8 or raw & 7 != 4:
                del self.buf[0]
                continue
            if len(self.buf) < size + 9:
                break
            if self.buf[size + 7:size + 9] != b'\r\n':
                del self.buf[0]
                continue
            result.append((raw >> 3, bytes(self.buf[7:size + 7])))
            del self.buf[:size + 9]
        return result

    def status(self, ident, data):
        if len(data) != 8 or ((ident >> 8) & 255) != MOTOR:
            return None
        kind = (ident >> 24) & 31
        if kind == 21:
            fault, warning = struct.unpack('<II', data)
            if fault or warning:
                raise RuntimeError(f'Motor fault={fault:08X} warning={warning:08X}')
        if kind in (2, 24):
            if (ident >> 16) & 63:
                raise RuntimeError(f'Motor feedback fault flags: {ident:08X}')
            return (ident >> 22) & 3
        return None

    def wait(self, predicate, timeout=.4):
        began = time.monotonic()
        while time.monotonic() - began < timeout:
            for ident, data in self.frames():
                self.status(ident, data)
                if predicate(ident, data):
                    return ident, data
        raise TimeoutError('No matching motor reply; no further enable allowed')

    def read(self, index, fmt):
        self.send(17, struct.pack('<HHI', index, 0, 0))
        ident, data = self.wait(lambda ident, data:
            len(data) == 8 and ((ident >> 24) & 31) == 17 and
            (ident & 0xFFFF) == ((MOTOR << 8) | HOST) and
            struct.unpack_from('<H', data)[0] == index)
        if (ident >> 16) & 255:
            raise RuntimeError(f'Parameter 0x{index:04X} read rejected')
        return struct.unpack_from('<' + fmt, data, 4)[0]

    def set(self, index, value, fmt):
        data = struct.pack('<HH', index, 0) + struct.pack('<' + fmt, value).ljust(4, b'\0')
        self.send(18, data)
        actual = self.read(index, fmt)
        if not math.isfinite(actual) or not math.isclose(actual, value, rel_tol=1e-5, abs_tol=1e-6):
            raise RuntimeError(f'Parameter 0x{index:04X} readback mismatch')
        print(f'VERIFY 0x{index:04X}: OK')

    def state(self, kind, expected):
        self.port.reset_input_buffer()
        self.buf.clear()
        self.send(kind)
        self.wait(lambda ident, data: self.status(ident, data) == expected)
        print('STOP confirmed' if expected == 0 else 'RUN confirmed at ZERO speed')

    def reports(self, expected):
        count = 0
        began = time.monotonic()
        while time.monotonic() - began < .4:
            for ident, data in self.frames():
                mode = self.status(ident, data)
                if mode is not None:
                    if mode != expected:
                        raise RuntimeError('Unexpected mode in automatic reports')
                    count += 1
            if count >= 3 and time.monotonic() - began >= .03:
                print(f'Automatic feedback verified: mode={expected}')
                return
        raise RuntimeError('Automatic reporting NOT verified; test aborted')


def run(port):
    bus = Adapter(port)
    lost = False
    try:
        bus.state(4, 0)
        bus.set(0x7028, 20000, 'I')
        bus.set(0x7018, 1.0, 'f')
        bus.set(0x7005, 2, 'B')
        bus.set(0x700A, 0.0, 'f')
        bus.port.reset_input_buffer()
        bus.buf.clear()
        bus.send(24, REPORT)
        bus.reports(0)
        bus.state(3, 2)
        bus.set(0x7018, 1.0, 'f')
        if bus.read(0x7028, 'I') != 20000:
            raise RuntimeError('Timeout changed after enable')
        bus.reports(2)
        print('\nZERO-SPEED HEARTBEAT ACTIVE. Unplug ONLY the USB-CAN adapter CAN-bus connector.')
        print('Keep motor power and Stick/Mini CAN observer connected. Ctrl+C requests stop while connected.')
        began = last_feedback = time.monotonic()
        next_tx = began
        while time.monotonic() - began < 60:
            now = time.monotonic()
            if now - last_feedback > .25:
                lost = True
                print('LINK LOST: transmitting stopped; script will exit. NO automatic reconnect or enable.')
                print('Wait for Stick DISABLED feedback, then reconnect CAN with this script CLOSED.')
                return
            if now >= next_tx:
                bus.send(18, struct.pack('<HHf', 0x700A, 0, 0.0))
                next_tx = time.monotonic() + .05
            for ident, data in bus.frames():
                mode = bus.status(ident, data)
                if mode is not None:
                    if mode != 2:
                        raise RuntimeError('Motor left RUN while link was active')
                    last_feedback = time.monotonic()
        raise RuntimeError('60-second limit reached without disconnect')
    finally:
        if not lost:
            try:
                bus.state(4, 0)
            except Exception as exc:
                print(f'STOP UNCONFIRMED: disconnect motor power. {exc}')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: py watchdog_disconnect.py COM3 (USB-CAN adapter port, NOT the Stick port)')
    print('RS05 ZERO-SPEED DISCONNECT TEST. Motor may energize at up to 1 A.')
    print('Stick must run WatchdogObserver; MotorStudio and other adapter programs must be closed.')
    input('Motor secured/unloaded, power cutoff accessible, observer ready? Press Enter to begin, or Ctrl+C to exit: ')
    import serial
    try:
        with serial.Serial(sys.argv[1], 921600, timeout=.005, write_timeout=.15,
                           rtscts=False, dsrdtr=False) as port:
            run(port)
    except KeyboardInterrupt:
        print('Operator stopped test.')
    except Exception as exc:
        print(f'TEST ABORTED: {exc}')
