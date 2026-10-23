import contextlib
import importlib.util
import io
import pathlib
import struct

path = pathlib.Path(__file__).resolve().parents[1] / 'scripts' / 'watchdog_disconnect.py'
spec = importlib.util.spec_from_file_location('disconnect', path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Clock:
    now = 0.
    def monotonic(self): return self.now
    def sleep(self, seconds): self.now += seconds

class Port:
    def __init__(self, clock, auto=True, reject=False):
        self.clock, self.auto, self.reject = clock, auto, reject
        self.buf = bytearray()
        self.params = {}
        self.mode = 0
        self.reporting = False
        self.last_report = 0.
        self.disconnect_at = None
        self.writes = []
    @property
    def connected(self): return self.disconnect_at is None or self.clock.now < self.disconnect_at
    @property
    def in_waiting(self): return len(self.buf)
    def reset_input_buffer(self): self.buf.clear()
    def frame(self, ident, data=bytes(8)):
        self.buf += b'AT' + struct.pack('>I', ident << 3 | 4) + bytes([len(data)]) + data + b'\r\n'
    def feedback(self, kind=2): self.frame(kind << 24 | self.mode << 22 | 1 << 8 | 126)
    def read(self, count):
        self.clock.now += .005
        if self.auto and self.reporting and self.connected and self.clock.now - self.last_report >= .01:
            self.last_report = self.clock.now
            self.feedback(24)
        out = bytes(self.buf[:count]); del self.buf[:count]
        return out
    def write(self, packet):
        assert packet[:2] == b'AT' and packet[-2:] == b'\r\n' and packet[6] == 8
        ident = struct.unpack_from('>I', packet, 2)[0] >> 3
        kind, data = ident >> 24, packet[7:15]
        assert ident & 0xFFFF == 0x7e01
        assert kind in (3,4,17,18,24)
        self.writes.append((kind,data))
        if not self.connected: return len(packet)
        index = struct.unpack_from('<H', data)[0]
        if kind == 4: self.mode=0; self.feedback()
        if kind == 3:
            assert self.params[0x7005] == b'\x02\0\0\0'
            assert self.params[0x700A] == bytes(4)
            assert self.params[0x7018] == struct.pack('<f',1.)
            assert self.params[0x7028] == struct.pack('<I',20000)
            self.mode=2; self.disconnect_at=self.clock.now+.6; self.feedback()
        if kind == 18:
            assert index in (0x7005,0x700A,0x7018,0x7028)
            if index==0x700A: assert data[4:]==bytes(4)
            self.params[index]=data[4:]; self.feedback()
        if kind == 17:
            response = data[:4]+self.params.get(index,bytes(4))
            self.frame(0x1100017e | (0x10000 if self.reject else 0),response)
        if kind == 24:
            assert data==m.REPORT; self.reporting=True; self.feedback(24)
        return len(packet)

clock=Clock(); m.time=clock; port=Port(clock)
with contextlib.redirect_stdout(io.StringIO()) as output: m.run(port)
assert 'LINK LOST' in output.getvalue()
assert sum(kind==3 for kind,data in port.writes)==1
assert sum(kind==4 for kind,data in port.writes)==1  # no stop/control transmission after detected loss
assert clock.now-port.disconnect_at < .4

for options in ({'auto':False},{'reject':True}):
    clock=Clock();m.time=clock;port=Port(clock,**options)
    try:
        with contextlib.redirect_stdout(io.StringIO()):m.run(port)
    except (RuntimeError,TimeoutError): pass
    else: raise AssertionError('failed prerequisite must abort')
    assert not any(kind==3 for kind,data in port.writes)

clock=Clock();m.time=clock;port=Port(clock);adapter=m.Adapter(port)
payload=b'AT\r\n\x00\xff\x00\x00'
port.frame(0x1100017e,payload)
frames=[]
for _ in range(20):frames+=adapter.frames()
assert frames==[(0x1100017e,payload)]
try: adapter.send(18,struct.pack('<HHf',0x700A,0,1.))
except ValueError: pass
else:raise AssertionError('nonzero speed accepted')
print('PASS: AT parser with embedded delimiters; verified zero-speed/current/timeout prerequisites; reporting required before enable; link loss exits without further commands or auto-enable; rejected reads block enable; nonzero speed forbidden. Simulation only.')
