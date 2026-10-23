#!/usr/bin/env python3
"""RS05 bench control GUI for the official CH340 USB-CAN AT adapter.
Windows: py -m pip install pyserial pygame
         py rs05_control_gui.py
Offline: py rs05_control_gui.py --demo
Checks:  py rs05_control_gui.py --self-test

Uses existing adapter settings: 921600 baud serial, 1 Mbps CAN, private
RobStride protocol. Close MotorStudio and other serial programs first.
Position is relative to the position measured on Connect; no motor zero is
rewritten. Configurable optional software endpoints limit position targets.
Displayed angle wraps at 360 degrees; total travel and targets remain signed
and multi-turn. Continuous speed is blocked while endpoints are enabled.
Angle inputs and application-level targets use whole degrees (nearest integer,
halfway away from zero). Raw motor feedback, the zero reference, and CAN radian
values retain precision. Re-zeroed endpoints round inward, never expanding
the previous physical interval. Rounding is not a motor settling controller.
Software endpoints constrain commands, not physical overshoot.
Reverse inputs changes the user coordinate direction, with acknowledged disable
first; it does not write a motor direction parameter. The applied endpoint
interval retains its physical location. Sweep plans are visual previews only.
Out-of-range sampled feedback requests torque disable; this is not a hard stop.
Four session-only saved positions require enabled endpoints and reuse signed
position targets, never wrapped-angle shortcuts. Reconnect, zero, and reverse
invalidate all slots. No hardware homing or persistent reference is assumed.
Optional pygame gamepad support uses devices paired through the OS, including
Bluetooth. Verify raw axis/button mappings before enabling. Centered stick maps
to the endpoint midpoint. Slot button mapping is configurable. A separate hold
button is required; releasing it or losing app focus requests torque disable.
Controller commands update position targets without repeated mode switching.
Speed mode rotates continuously. This is a bench tool, not a real-time robot
controller. First test with the mechanism unloaded and power cutoff accessible.
Disable releases torque; it is not a guaranteed brake or hardware emergency stop.

Sends the documented 1-second CAN timeout (0x7028 = 20000). That parameter is
write-only in the RS05 manual: its action MUST be checked on actual firmware
with an unloaded motor before relying on it. No automatic fault clearing,
calibration, ID changes, firmware changes, or parameter-save commands.
Protocol references: https://github.com/RobStride/Python_Sample
ROBSTRIDE 05 User Manual, sections 3.3.6 and 4.1 / 4.3.
Software tested with simulated serial responses; physical hardware untested.
"""
import os
import math
import struct
import sys
import time
import queue
import threading
import tkinter as tk
from tkinter import ttk, messagebox

HOST = 0xFD
RPM = 2 * math.pi / 60

def encode(can_id, data):
    if not 0 <= can_id <= 0x1FFFFFFF or len(data) > 8:
        raise ValueError('Invalid classic CAN frame')
    return b'AT' + struct.pack('>I', (can_id << 3) | 4) + bytes([len(data)]) + data + b'\r\n'


class FrameParser:
    """Length-based binary parser: embedded CR/LF and AT in payload are valid."""
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, chunk):
        self.buffer.extend(chunk)
        frames = []
        while True:
            start = self.buffer.find(b'AT')
            if start < 0:
                self.buffer[:] = b'A' if self.buffer.endswith(b'A') else b''
                break
            del self.buffer[:start]
            if len(self.buffer) < 7:
                break
            raw_id = struct.unpack('>I', self.buffer[2:6])[0]
            dlc = self.buffer[6]
            if dlc > 8 or (raw_id & 7) != 4:
                del self.buffer[0]
                continue
            size = 9 + dlc
            if len(self.buffer) < size:
                break
            if self.buffer[size-2:size] != b'\r\n':
                del self.buffer[0]
                continue
            frames.append((raw_id >> 3, bytes(self.buffer[7:7+dlc])))
            del self.buffer[:size]
        return frames


class Cancelled(Exception):
    pass


class Bus:
    def __init__(self, serial_port, motor, guard=lambda: None, trace=lambda text: None):
        self.serial, self.motor, self.guard = serial_port, motor, guard
        self.parser = FrameParser()
        self.trace = trace
        self.last_tx = 0.

    def send(self, kind, data=bytes(8)):
        # The USB adapter bridges asynchronous serial and CAN queues.
        # Avoid back-to-back writes/reads outrunning its firmware.
        while time.monotonic() - self.last_tx < .025:
            if kind != 4: self.guard()
            time.sleep(.002)
        self.serial.write(encode((kind << 24) | (HOST << 8) | self.motor, data))
        self.last_tx = time.monotonic()

    def receive(self, predicate, check=True):
        deadline = time.monotonic() + .22
        while time.monotonic() < deadline:
            if check:
                self.guard()
            for ident, data in self.parser.feed(self.serial.read(
                    min(max(self.serial.in_waiting, 1), 1024))):
                if ((ident >> 8) & 255) != self.motor or (ident & 255) != HOST:
                    continue
                kind = (ident >> 24) & 31
                if check and kind == 2 and ((ident >> 16) & 63):
                    raise RuntimeError(f'Motor fault flags 0x{(ident >> 16) & 63:02X}. Check MotorStudio.')
                if predicate(kind, ident, data):
                    return ident, data
        raise TimeoutError('No matching motor reply. Check power, CAN wiring, ID and adapter settings.')

    def discard(self):
        self.serial.reset_input_buffer()
        self.parser = FrameParser()

    def read(self, index, fmt='f'):
        for attempt in range(3):
            self.guard()
            self.discard()
            self.send(17, struct.pack('<HHI', index, 0, 0))
            try:
                _, data = self.receive(lambda kind, ident, data:
                    kind == 17 and len(data) == 8 and struct.unpack('<H', data[:2])[0] == index)
            except TimeoutError:
                self.trace(f'Read 0x{index:04X}: no reply (attempt {attempt + 1}/3)')
                if attempt == 2:
                    raise TimeoutError(f'Reading parameter 0x{index:04X}: no reply after 3 attempts.') from None
                continue
            value = struct.unpack('<' + fmt, data[4:4+struct.calcsize(fmt)])[0]
            if not math.isfinite(value):
                raise RuntimeError(f'Parameter 0x{index:04X} returned an invalid value.')
            return value

    def write(self, index, value, fmt='f', verify=False):
        self.guard()
        data = struct.pack('<HH', index, 0) + struct.pack('<' + fmt, value).ljust(4, b'\0')
        self.trace(f'Write 0x{index:04X} = {value:g}' + ('; checking readback' if verify else ''))
        self.send(18, data)
        if verify:
            actual = self.read(index, fmt)
            if not math.isclose(actual, value, rel_tol=1e-4, abs_tol=1e-5):
                raise RuntimeError(f'Parameter 0x{index:04X}: requested {value:g}, read back {actual:g}. Motion aborted.')

    def disable(self):
        self.discard()
        self.send(4)
        self.receive(lambda kind, ident, data: kind == 2 and ((ident >> 22) & 3) == 0,
                     check=False)

    def enable(self):
        self.guard()
        self.discard()
        self.trace('Enable motor: waiting for RUN-state feedback')
        self.send(3)
        try:
            self.receive(lambda kind, ident, data: kind == 2 and ((ident >> 22) & 3) == 2)
        except TimeoutError:
            raise TimeoutError('Enable command: no RUN-state acknowledgement from motor.') from None


class DemoSerial:
    """In-memory protocol simulator. Never opens a COM port."""
    def __init__(self):
        self.rx = bytearray()
        self.params = {0x7019: 0., 0x701B: 0., 0x701C: 24.6, 0x701A: 0.}
        self.enabled = False
        self.last = time.monotonic()
        self.history = []
        self.closed = False

    @property
    def in_waiting(self): return len(self.rx)
    def reset_input_buffer(self): self.rx.clear()
    def close(self): self.closed = True
    def read(self, count):
        data = bytes(self.rx[:count]); del self.rx[:count]
        if not data: time.sleep(.001)
        return data
    def write(self, packet):
        ident, data = FrameParser().feed(packet)[0]
        kind, motor = ident >> 24, ident & 255
        self.history.append((kind, data))
        now = time.monotonic(); dt = min(now - self.last, .2); self.last = now
        speed = 0.
        if self.enabled:
            if self.params.get(0x7005) == 2:
                speed = self.params.get(0x700A, 0.)
            else:
                delta = self.params.get(0x7016, 0.) - self.params[0x7019]
                limit = self.params.get(0x7017, .5)
                speed = max(-limit, min(limit, delta / max(dt, .00001)))
        self.params[0x7019] += speed * dt
        self.params[0x701B] = speed
        if kind in (17, 18):
            index = struct.unpack('<H', data[:2])[0]
            fmt = 'B' if index == 0x7005 else 'I' if index == 0x7028 else 'f'
            if kind == 18:
                self.params[index] = struct.unpack('<' + fmt, data[4:4+struct.calcsize(fmt)])[0]
            else:
                payload = struct.pack('<HH', index, 0) + struct.pack('<' + fmt, self.params.get(index, 0)).ljust(4, b'\0')
                self.rx.extend(encode((17 << 24) | (motor << 8) | HOST, payload))
        if kind in (3, 4):
            self.enabled = kind == 3
            state = 2 if self.enabled else 0
            self.rx.extend(encode((2 << 24) | (state << 22) | (motor << 8) | HOST, bytes(8)))
        return len(packet)


def bounded(value, low, high, label):
    n = float(value)
    if not math.isfinite(n) or not low <= n <= high:
        raise ValueError(f'{label} must be between {low} and {high}.')
    return n


def whole_degrees(value):
    """Round halves away from zero, consistently for both directions."""
    n = float(value)
    if not math.isfinite(n):
        raise ValueError('Angle must be a finite number.')
    return math.floor(n + .5) if n >= 0 else math.ceil(n - .5)


def degree_value(value, low=-1000000000, high=1000000000, label='Angle'):
    return int(bounded(whole_degrees(value), low, high, label))


def degree_limits(low, high):
    low = degree_value(low, label='Minimum')
    high = degree_value(high, label='Maximum')
    if low >= high:
        raise ValueError('Minimum must be less than maximum after rounding to whole degrees.')
    return low, high


def rebased_limits(limits, shift):
    # Integer endpoints must not expand the existing physical interval.
    return degree_limits(math.ceil(limits[0] + shift), math.floor(limits[1] + shift))


class Worker(threading.Thread):
    def __init__(self, demo=False):
        super().__init__(daemon=True)
        self.demo = demo
        self.commands = queue.Queue(maxsize=1)
        self.events = queue.Queue()
        self.stop_now = threading.Event()
        self.quit_now = threading.Event()
        self.heartbeat = time.monotonic()
        self.bus = None
        self.active = False
        self.origin = 0.
        self.target = None
        self.next_poll = 0.
        self.endpoints = (-180, 180)
        self.direction = 1
        self.pad_deadline = None
        self.saved_positions = [None] * 4

    def guard(self):
        if self.pad_deadline is not None and time.monotonic() > self.pad_deadline:
            raise RuntimeError('Controller heartbeat expired; control stopped.')
        if self.stop_now.is_set() or self.quit_now.is_set():
            raise Cancelled()
        if time.monotonic() - self.heartbeat > .6:
            raise RuntimeError('GUI stopped responding; control stopped.')

    def emit(self, kind, value): self.events.put((kind, value))

    def clear_commands(self):
        while True:
            try: self.commands.get_nowait()
            except queue.Empty: return

    def stop_motor(self):
        self.pad_deadline = None
        self.active = False
        self.target = None
        if self.bus:
            self.bus.disable()
            self.emit('status', 'Disabled — torque released')

    def disconnect(self):
        if self.bus:
            try: self.stop_motor()
            finally:
                self.bus.serial.close()
                self.bus = None
                self.emit('connected', False)

    def controller_target(self, value, current, speed, acceleration):
        self.guard()
        if not self.endpoints: raise ValueError('Controller requires applied endpoints.')
        value = degree_value(value)
        bounded(value, *self.endpoints, 'Controller target')
        if not self.active or self.target is None or self.target[0] != 0x7016:
            self.motion('position', value, current, speed, acceleration)
        else:
            measured = self.direction * math.degrees(self.bus.read(0x7019) - self.origin)
            bounded(measured, *self.endpoints, 'Measured position')
            target = self.origin + self.direction * math.radians(value)
            self.bus.write(0x7016, target)
            self.target = (0x7016, target)
        self.pad_deadline = time.monotonic() + .4
        self.emit('saved_target', value)
        self.emit('status', f'ENABLED — controller position: {value} deg')

    def clear_saved_positions(self):
        self.saved_positions = [None] * 4
        self.emit('saved_positions', tuple(self.saved_positions))

    def save_position(self, slot, name, value):
        if not self.bus: raise ValueError('Connect first.')
        if not isinstance(slot, int) or not 0 <= slot < 4: raise ValueError('Invalid position slot.')
        if not self.endpoints: raise ValueError('Apply endpoints before saving positions.')
        if value is None:
            value = self.direction * math.degrees(self.bus.read(0x7019) - self.origin)
            bounded(value, *self.endpoints, 'Measured position')
        value = degree_value(value, label='Saved position')
        bounded(value, *self.endpoints, 'Saved position')
        self.saved_positions[slot] = (str(name).strip()[:32] or f'Position {slot + 1}', value)
        self.emit('saved_positions', tuple(self.saved_positions))
        self.emit('log', f'Saved position {slot + 1}: {value}° (no motion)')

    def move_saved(self, slot, current, speed, acceleration):
        if not isinstance(slot, int) or not 0 <= slot < 4: raise ValueError('Invalid position slot.')
        if not self.endpoints: raise ValueError('Saved moves require enabled endpoints.')
        saved = self.saved_positions[slot]
        if saved is None: raise ValueError('Save this position first.')
        # A signed interval is convex: both endpoints inside implies the commanded
        # segment stays inside. Never modulo or choose a shortest circular route.
        bounded(saved[1], *self.endpoints, 'Saved target')
        self.motion('position', saved[1], current, speed, acceleration)
        self.emit('saved_target', saved[1])

    def motion(self, mode, value, current, speed, acceleration):
        if not self.bus: raise RuntimeError('Connect first.')
        current = bounded(current, .1, 2., 'Current limit (A)')
        speed = bounded(speed, .1, 60., 'Position speed (RPM)')
        acceleration = bounded(acceleration, .1, 10., 'Acceleration (rad/s²)')
        if mode == 'position':
            value = degree_value(value, label='Position travel (degrees)')
            if self.endpoints:
                bounded(value, *self.endpoints, 'Position target')
            target = self.origin + self.direction * math.radians(value)
        else:
            value = bounded(value, -60., 60., 'Speed (RPM)')
            if self.endpoints and value != 0:
                raise ValueError('Turn off endpoints and Apply before continuous speed mode.')
            target = self.direction * value * RPM
        self.stop_motor()  # Never switch modes while enabled.
        b = self.bus
        self.emit('log', f'Preparing {mode} command')
        current_position = b.read(0x7019)
        if mode == 'position' and self.endpoints:
            bounded(self.direction * math.degrees(current_position - self.origin),
                    self.endpoints[0], self.endpoints[1],
                    'Current travel; reset zero or adjust endpoints')
        b.write(0x7005, 5 if mode == 'position' else 2, 'B', verify=True)
        b.write(0x7018, current, verify=True)
        b.write(0x7017, speed * RPM, verify=True)
        b.write(0x7022, acceleration)  # Write-only on documented firmware.
        b.write(0x7028, 20000, 'I')  # 1 second; documented write-only watchdog.
        index = 0x7016 if mode == 'position' else 0x700A
        b.write(index, current_position if mode == 'position' else 0., verify=True)
        b.enable()
        # Repeat limits after enable to accommodate firmware initialization.
        b.write(0x7018, current, verify=True)
        b.write(0x7017, speed * RPM, verify=True)
        b.write(0x7022, acceleration)
        b.write(index, target, verify=True)
        self.target = (index, target)
        self.active = True
        self.emit('status', f'ENABLED — {mode}: {value:g} ' + ('deg' if mode == 'position' else 'RPM'))

    def execute(self, command):
        name, args = command
        if name == 'connect':
            if self.bus: raise RuntimeError('Disconnect before changing the connection.')
            port, motor = args
            if not 0 <= motor <= 255 or motor == HOST:
                raise ValueError('Motor ID must be 0–255, excluding host ID 253.')
            if self.demo:
                serial_port = DemoSerial()
            else:
                try: import serial
                except ImportError:
                    raise RuntimeError('Install pyserial: py -m pip install pyserial') from None
                serial_port = serial.Serial(port, 921600, timeout=.01, write_timeout=.15,
                                            rtscts=False, dsrdtr=False)
            self.bus = Bus(serial_port, motor, self.guard, lambda text: self.emit('log', text))
            # Connecting deliberately disables any existing motor motion.
            self.stop_motor()
            self.origin = self.bus.read(0x7019)
            self.clear_saved_positions()
            self.emit('connected', True)
            self.emit('status', 'Connected / disabled — current position is 0° for this session')
        elif name == 'reverse':
            if not self.bus: raise ValueError('Connect first.')
            self.stop_motor()  # Require disable acknowledgement before changing coordinates.
            direction = -1 if args[0] else 1
            if direction != self.direction:
                if self.endpoints:
                    self.endpoints = (-self.endpoints[1], -self.endpoints[0])
                self.direction = direction
            self.clear_saved_positions()
            self.emit('direction', (self.direction, self.endpoints))
            self.emit('status', 'Disabled — input direction ' + ('reversed' if direction == -1 else 'normal'))
        elif name == 'zero':
            if not self.bus: raise ValueError('Connect first.')
            self.stop_motor()
            new_origin = self.bus.read(0x7019)
            if self.endpoints:
                # Preserve the physical interval, rounding inward to whole degrees.
                shift = self.direction * math.degrees(self.origin - new_origin)
                self.endpoints = rebased_limits(self.endpoints, shift)
            self.origin = new_origin
            self.clear_saved_positions()
            self.emit('reference', self.endpoints)
            self.emit('status', 'Disabled — current position is now 0°; endpoints shifted and rounded inward')
        elif name == 'endpoints':
            if not self.bus: raise ValueError('Connect first.')
            self.stop_motor()
            limits = args[0]
            if limits is not None:
                limits = degree_limits(*limits)
            self.endpoints = limits
            self.emit('status', 'Disabled — endpoints ' +
                      (f'{self.endpoints[0]:g}° to {self.endpoints[1]:g}°' if self.endpoints else 'off'))
        elif name == 'disconnect': self.disconnect()
        elif name == 'motion': self.motion(*args)
        elif name == 'save_position': self.save_position(*args)
        elif name == 'move_saved': self.move_saved(*args)
        elif name == 'controller': self.controller_target(*args)

    def run(self):
        try:
            while not self.quit_now.is_set():
                try:
                    if self.stop_now.is_set():
                        self.clear_commands()
                        self.stop_motor()
                        self.stop_now.clear()
                        if not self.bus: self.emit('status', 'Disconnected — no motor connection')
                        self.emit('ready', True)
                    try: command = self.commands.get(timeout=.02)
                    except queue.Empty: command = None
                    if command:
                        try: self.execute(command)
                        finally: self.emit('ready', True)
                    if self.bus:
                        self.guard()
                        if time.monotonic() >= self.next_poll:
                            values = [self.bus.read(i) for i in (0x7019, 0x701B, 0x701C, 0x701A)]
                            travel = self.direction * math.degrees(values[0] - self.origin)
                            if self.active and self.endpoints and not self.endpoints[0] <= travel <= self.endpoints[1]:
                                self.stop_motor()
                                self.emit('error', 'Measured position outside software endpoints — torque disabled. These limits cannot prevent overshoot.')
                            if self.active: self.bus.write(*self.target)
                            self.emit('readings', (whole_degrees(travel),
                                                  self.direction * values[1] / RPM, values[2], values[3]))
                            self.next_poll = time.monotonic() + .05
                except Cancelled:
                    continue
                except Exception as exc:
                    self.clear_commands()
                    try: self.disconnect()
                    except Exception as stop_exc:
                        self.emit('error', str(exc) + '\nSTOP UNCONFIRMED — use physical power cutoff. ' + str(stop_exc))
                    else: self.emit('error', str(exc))
                    self.stop_now.clear()
                    self.emit('ready', True)
        finally:
            try: self.disconnect()
            except Exception as exc: self.emit('error', 'STOP UNCONFIRMED — cut motor power. ' + str(exc))
            self.emit('closed', True)


def stick_target(axis, limits, deadzone=.12):
    axis = bounded(axis, -1, 1, 'Stick axis')
    if abs(axis) <= deadzone: axis = 0.
    else: axis = math.copysign((abs(axis) - deadzone) / (1 - deadzone), axis)
    low, high = limits
    return degree_value((low + high) / 2 + axis * (high - low) / 2, low, high)


class App:
    def __init__(self, root, demo=False):
        self.root = root
        self.worker = Worker(demo)
        self.connected = False
        self.busy = False
        self.closing = False
        self.position = None
        root.title('RS05 Control v1.6' + (' — DEMO (no hardware)' if demo else ''))
        root.geometry('950x820')
        root.minsize(900, 650)
        self.pending_error = None
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('TLabel', font=('Segoe UI', 10))
        style.configure('TButton', font=('Segoe UI', 10), padding=7)
        # Pin errors at the top so Windows scaling cannot hide diagnostics.
        self.status = tk.StringVar(value='DEMO — no hardware' if demo else 'Disconnected')
        ttk.Label(root, textvariable=self.status, wraplength=760,
                  font=('Segoe UI', 10, 'bold'), padding=8).pack(fill='x', side='top')
        viewport = ttk.Frame(root); viewport.pack(fill='both', expand=True)
        canvas = tk.Canvas(viewport, highlightthickness=0)
        scrollbar = ttk.Scrollbar(viewport, orient='vertical', command=canvas.yview)
        scrollbar.pack(side='right', fill='y'); canvas.pack(side='left', fill='both', expand=True)
        canvas.configure(yscrollcommand=scrollbar.set)
        outer = ttk.Frame(canvas, padding=12)
        content = canvas.create_window((0, 0), window=outer, anchor='nw')
        outer.bind('<Configure>', lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>', lambda e: canvas.itemconfigure(content, width=e.width))
        root.bind('<MouseWheel>', lambda e: canvas.yview_scroll(-int(e.delta / 120), 'units'))
        ttk.Label(outer, text='RS05  /  Motor control', font=('Segoe UI', 20, 'bold')).pack(anchor='w')
        ttk.Label(outer, text='Bench testing • degrees and RPM • one motor at a time').pack(anchor='w', pady=(2, 12))
        connection = ttk.LabelFrame(outer, text='Connection', padding=10); connection.pack(fill='x')
        self.port = tk.StringVar(value='COM3')
        self.motor = tk.StringVar(value='1')
        ttk.Label(connection, text='Port').pack(side='left')
        self.ports = ttk.Combobox(connection, textvariable=self.port, width=12)
        self.ports.pack(side='left', padx=6)
        ttk.Button(connection, text='Refresh', command=self.refresh).pack(side='left')
        ttk.Label(connection, text='Motor ID').pack(side='left', padx=(18, 4))
        self.id_entry = ttk.Entry(connection, textvariable=self.motor, width=5); self.id_entry.pack(side='left')
        self.connect_button = ttk.Button(connection, text='Connect', command=self.connect)
        self.connect_button.pack(side='right')
        live = ttk.LabelFrame(outer, text='Live readings', padding=12); live.pack(fill='x', pady=12)
        self.live = []
        for n, title in enumerate(('Angle (0–360°)', 'Speed (RPM)', 'Supply (V)', 'Phase current (A)')):
            live.columnconfigure(n, weight=1)
            ttk.Label(live, text=title).grid(row=0, column=n, sticky='w')
            value = tk.StringVar(value='—'); self.live.append(value)
            ttk.Label(live, textvariable=value, font=('Segoe UI', 20)).grid(row=1, column=n, sticky='w', pady=4)
        self.travel = tk.StringVar(value='Total travel: —')
        ttk.Label(outer, textvariable=self.travel).pack(anchor='w')
        self.zero_button = ttk.Button(outer, text='Set current position as 0° (disables motor)',
                                      command=lambda: self.submit('zero'))
        self.zero_button.pack(anchor='w', pady=4)
        self.reverse = tk.BooleanVar(value=False)
        self.reverse_button = ttk.Checkbutton(outer, text='Reverse motor inputs (disables motor; preserves physical endpoints)',
                                              variable=self.reverse, command=self.reverse_inputs)
        self.reverse_button.pack(anchor='w', pady=4)
        endpoints = ttk.LabelFrame(outer, text='Software endpoints — signed total travel from zero', padding=8)
        endpoints.pack(fill='x', pady=6)
        self.use_endpoints = tk.BooleanVar(value=True)
        ttk.Checkbutton(endpoints, text='Enable', variable=self.use_endpoints).grid(row=0, column=0)
        self.low = tk.StringVar(value='-180'); self.high = tk.StringVar(value='180')
        for col, label, var in ((1, 'Minimum °', self.low), (3, 'Maximum °', self.high)):
            ttk.Label(endpoints, text=label).grid(row=0, column=col)
            ttk.Entry(endpoints, textvariable=var, width=11).grid(row=0, column=col+1, padx=4)
        self.endpoint_button = ttk.Button(endpoints, text='Apply (disables)', command=self.apply_endpoints)
        self.endpoint_button.grid(row=0, column=5, padx=4)
        ttk.Button(endpoints, text='Use current as minimum', command=lambda: self.capture(self.low)).grid(row=1, column=1, columnspan=2)
        ttk.Button(endpoints, text='Use current as maximum', command=lambda: self.capture(self.high)).grid(row=1, column=3, columnspan=2)
        ttk.Label(endpoints, text='Apply to activate changes. Enabled endpoints block continuous speed; physical overshoot is possible.').grid(row=2, column=0, columnspan=6, sticky='w')
        limits = ttk.LabelFrame(outer, text='Limits — applied when you press a motion button', padding=10)
        limits.pack(fill='x')
        self.current = self.field(limits, 'Current (0.1–2 A)', '0.5', 0)
        self.max_speed = self.field(limits, 'Position speed (0.1–60 RPM)', '5', 1)
        self.acceleration = self.field(limits, 'Speed acceleration (0.1–10 rad/s²)', '1', 2)
        controls = ttk.Frame(outer); controls.pack(fill='x', pady=12)
        controls.columnconfigure(0, weight=1); controls.columnconfigure(1, weight=1)
        pos = ttk.LabelFrame(controls, text='Position', padding=10); pos.grid(row=0, column=0, sticky='nsew', padx=(0, 6))
        vel = ttk.LabelFrame(controls, text='Continuous speed', padding=10); vel.grid(row=0, column=1, sticky='nsew', padx=(6, 0))
        self.angle = tk.StringVar(value='0'); self.step = tk.StringVar(value='5'); self.speed = tk.StringVar(value='5')
        ttk.Label(pos, text='Target travel (whole ° from zero; signed / multi-turn)').pack(anchor='w')
        ttk.Entry(pos, textvariable=self.angle, width=15).pack(anchor='w', pady=5)
        self.motion_buttons = []
        self.add_motion(pos, 'Move to position', lambda: self.move('position', self.angle.get())).pack(fill='x')
        row = ttk.Frame(pos); row.pack(fill='x', pady=(8, 0))
        ttk.Label(row, text='Jog step (whole °)').pack(side='left')
        ttk.Entry(row, textvariable=self.step, width=6).pack(side='left', padx=5)
        self.add_motion(row, '−', lambda: self.jog(-1)).pack(side='left')
        self.add_motion(row, '+', lambda: self.jog(1)).pack(side='left', padx=4)
        ttk.Label(vel, text='Target RPM (−60 to +60)').pack(anchor='w')
        ttk.Entry(vel, textvariable=self.speed, width=15).pack(anchor='w', pady=5)
        self.add_motion(vel, 'Run at speed', lambda: self.move('speed', self.speed.get())).pack(fill='x')
        self.add_motion(vel, 'Set speed to 0 RPM', lambda: self.move('speed', '0')).pack(fill='x', pady=(8, 0))
        ttk.Label(outer, text='Angles round to whole degrees. Angle wraps at 360°; targets/endpoints use total travel.\nZero shifts endpoints and rounds inward. Motion buttons enable torque.').pack(anchor='w')
        tk.Button(root, text='DISABLE MOTOR  [Esc]', bg='#b4232c', fg='white',
                  activebackground='#861923', activeforeground='white', font=('Segoe UI', 14, 'bold'),
                  relief='flat', pady=10, command=self.stop).pack(side='bottom', fill='x', pady=(6, 6))
        self.pg = None; self.joy = None; self.pad_driving = False
        self.pad_released = False; self.pad_last = None; self.pad_buttons = set()
        self.pad_next = 0.; self.pad_preset = None
        pad = ttk.LabelFrame(outer, text='Bluetooth gamepad — pair in Windows first', padding=8)
        pad.pack(fill='x', pady=6)
        self.pad_devices = ttk.Combobox(pad, state='readonly', width=42)
        self.pad_devices.grid(row=0, column=0, columnspan=3, sticky='ew')
        ttk.Button(pad, text='Find controllers', command=self.find_controllers).grid(row=0,column=3)
        ttk.Button(pad, text='Use selected', command=self.select_controller).grid(row=0,column=4)
        self.pad_enabled = tk.BooleanVar(value=False)
        ttk.Checkbutton(pad, text='Enable gamepad control', variable=self.pad_enabled, command=self.toggle_pad).grid(row=1,column=0,columnspan=3,sticky='w')
        self.pad_axis = tk.StringVar(value='0'); self.pad_hold = tk.StringVar(value='4')
        self.pad_faces = tk.StringVar(value='0,1,2,3')
        for col, label, var in ((0,'Position axis',self.pad_axis),(2,'Hold button',self.pad_hold)):
            ttk.Label(pad,text=label).grid(row=2,column=col)
            ttk.Entry(pad,textvariable=var,width=5).grid(row=2,column=col+1)
        ttk.Label(pad,text='Slots 1–4 buttons').grid(row=3,column=0)
        ttk.Entry(pad,textvariable=self.pad_faces,width=18).grid(row=3,column=1,columnspan=2)
        self.pad_status = tk.StringVar(value='Not connected. Live monitor works with control disabled.')
        ttk.Label(pad,textvariable=self.pad_status,wraplength=800).grid(row=4,column=0,columnspan=5,sticky='w')
        ttk.Label(pad,text='Center = midpoint of applied endpoints; stick extremes = endpoints. Hold button required.\nRelease / loss / leaving this window disables torque. Face buttons select saved positions; moving stick resumes stick control.\nVerify live button/axis numbers before enabling. Mapping edits disarm control.').grid(row=5,column=0,columnspan=5,sticky='w')
        for var in (self.pad_axis,self.pad_hold,self.pad_faces):
            var.trace_add('write', lambda *args: self.disarm_pad())
        saved_frame = ttk.LabelFrame(outer, text='Four saved positions — current connection only', padding=8)
        saved_frame.pack(fill='x', pady=6)
        self.saved_slots = [None] * 4
        self.slot_names = []; self.slot_angles = []; self.slot_move_buttons = []; self.slot_save_buttons = []
        for i in range(4):
            saved_frame.columnconfigure(i, weight=1)
            cell = ttk.Frame(saved_frame, padding=4); cell.grid(row=0, column=i, sticky='nsew')
            name = tk.StringVar(value=f'Position {i+1}'); angle = tk.StringVar(value='0')
            self.slot_names.append(name); self.slot_angles.append(angle)
            ttk.Entry(cell, textvariable=name, width=18).pack(fill='x')
            ttk.Label(cell, text='Signed target °').pack(anchor='w')
            ttk.Entry(cell, textvariable=angle, width=10).pack(fill='x')
            save = ttk.Button(cell, text='Save target', command=lambda slot=i: self.save_slot(slot))
            save.pack(fill='x'); self.slot_save_buttons.append(save)
            capture = ttk.Button(cell, text='Save current', command=lambda slot=i: self.save_slot(slot, True))
            capture.pack(fill='x'); self.slot_save_buttons.append(capture)
            move = ttk.Button(cell, text=f'Move {i+1} — not saved', command=lambda slot=i: self.move_slot(slot))
            move.pack(fill='x', pady=(6,0)); self.slot_move_buttons.append(move)
        ttk.Label(saved_frame, text='Save does not move. Move uses applied endpoints and fresh motor feedback.\nSlots clear on reconnect, zero, or reverse. Unsaved field edits do not change a stored position.').grid(row=1,column=0,columnspan=4,sticky='w')
        arc_frame = ttk.LabelFrame(outer, text='Arc and sweep planner — preview only, does not move the motor', padding=8)
        arc_frame.pack(fill='x', pady=6)
        self.arc = tk.Canvas(arc_frame, width=820, height=230, bg='#f5f7fa', highlightthickness=0)
        self.arc.pack(fill='x')
        self.sweeps = []
        self.sweep_start = tk.StringVar(value='0'); self.sweep_end = tk.StringVar(value='25')
        row = ttk.Frame(arc_frame); row.pack(fill='x')
        ttk.Label(row, text='Sweep start °').pack(side='left')
        ttk.Entry(row, textvariable=self.sweep_start, width=8).pack(side='left')
        ttk.Label(row, text='End °').pack(side='left', padx=(10, 0))
        ttk.Entry(row, textvariable=self.sweep_end, width=8).pack(side='left')
        ttk.Button(row, text='Add preview sweep', command=self.add_sweep).pack(side='left', padx=8)
        ttk.Button(row, text='Remove selected', command=self.remove_sweep).pack(side='left')
        self.sweep_list = tk.Listbox(arc_frame, height=3, exportselection=False)
        self.sweep_list.pack(fill='x', pady=4)
        self.sweep_list.bind('<<ListboxSelect>>', lambda event: self.draw_arc())
        ttk.Label(arc_frame, text='Green: applied allowed arc  •  Blue: measured  •  Orange: target  •  Purple: selected sweep\nArc wraps visually; signed total-travel labels distinguish turns. Software limits are not hard stops.').pack(anchor='w')
        self.arc.bind('<Configure>', lambda event: self.draw_arc())
        self.angle.trace_add('write', lambda *args: self.draw_arc())
        self.log = tk.Text(outer, height=4, wrap='word', font=('Consolas', 9), state='disabled')
        self.log.pack(fill='both', expand=True, pady=(5, 0))
        self.log_message('Test unloaded. Disable releases torque; use a physical cutoff for emergencies.')
        self.log_message('CAN watchdog requested: 1 s. Verify its action on your motor firmware before relying on it.')
        root.bind('<Escape>', lambda event: self.stop())
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.refresh(); self.update_controls(); self.worker.start(); self.tick()

    def disarm_pad(self):
        self.pad_enabled.set(False)
        if self.pad_driving: self.worker.stop_now.set()
        self.pad_driving = False; self.pad_released = False; self.pad_last = None
        self.pad_preset = None

    def find_controllers(self):
        self.disarm_pad()
        try:
            if self.pg is None:
                os.environ.setdefault('SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS', '1')
                import pygame
                self.pg = pygame
                pygame.display.init(); pygame.joystick.init()
            self.pg.event.pump()
            self.pad_devices['values'] = [f'{i}: {self.pg.joystick.Joystick(i).get_name()}'
                                         for i in range(self.pg.joystick.get_count())]
            if self.pg.joystick.get_count(): self.pad_devices.current(0)
            else: self.pad_status.set('No gamepad detected. Pair it in Windows, then Find controllers.')
        except Exception as exc:
            self.pad_status.set(f'{exc}. Install dependency: py -m pip install pygame')

    def select_controller(self):
        self.disarm_pad()
        try:
            if self.pg is None: raise ValueError('Click Find controllers first.')
            index = self.pad_devices.current()
            if index < 0: raise ValueError('Select a detected controller.')
            if self.joy: self.joy.quit()
            self.joy = self.pg.joystick.Joystick(index)
            self.pad_buttons = set()
            self.pad_status.set('Selected ' + self.joy.get_name() + '. Verify inputs in live monitor.')
        except Exception as exc: self.pad_status.set(str(exc))

    def toggle_pad(self):
        if not self.pad_enabled.get(): self.disarm_pad(); return
        if not self.connected or not self.worker.endpoints or self.joy is None or self.busy:
            self.disarm_pad()
            self.pad_status.set('Connect motor, apply endpoints, select gamepad, then enable control.')
            return
        # Stop prior GUI motion before the gamepad is allowed to take over.
        self.worker.stop_now.set()
        self.pad_released = False; self.pad_last = None; self.pad_preset = None
        self.pad_status.set('Release hold button and center stick, then hold to command midpoint.')

    def poll_pad(self):
        if self.pg is None or self.joy is None: return
        try:
            for event in self.pg.event.get():
                if event.type == self.pg.JOYDEVICEREMOVED and event.instance_id == self.joy.get_instance_id():
                    raise RuntimeError('Gamepad disconnected')
            # Device indices may change; verify the selected instance still exists.
            ids = [self.pg.joystick.Joystick(i).get_instance_id() for i in range(self.pg.joystick.get_count())]
            if self.joy.get_instance_id() not in ids: raise RuntimeError('Gamepad disconnected')
            axes = [self.joy.get_axis(i) for i in range(self.joy.get_numaxes())]
            buttons = {i for i in range(self.joy.get_numbuttons()) if self.joy.get_button(i)}
            pressed = buttons - self.pad_buttons; self.pad_buttons = buttons
            self.pad_status.set('Axes: ' + ' '.join(f'{i}:{v:+.2f}' for i,v in enumerate(axes)) +
                                ' | Buttons: ' + ','.join(map(str,sorted(buttons))))
            if not self.pad_enabled.get(): return
            if self.root.focus_displayof() is None or not self.connected or not self.worker.endpoints:
                self.disarm_pad(); return
            axis_index = int(self.pad_axis.get()); hold = int(self.pad_hold.get())
            faces = [int(v.strip()) for v in self.pad_faces.get().split(',')]
            if not 0 <= axis_index < len(axes) or not 0 <= hold < self.joy.get_numbuttons():
                raise ValueError('Axis or hold button index is out of range')
            if len(faces) != 4 or len(set(faces)) != 4 or hold in faces or any(v < 0 or v >= self.joy.get_numbuttons() for v in faces):
                raise ValueError('Choose four distinct valid slot buttons, separate from hold')
            axis = axes[axis_index]
            if hold not in buttons:
                if self.pad_driving: self.worker.stop_now.set()
                self.pad_driving = False; self.pad_last = None; self.pad_preset = None
                self.pad_released = abs(axis) <= .12
                return
            if not self.pad_released: return
            self.pad_driving = True
            # Refreshed only after a successful input poll with hold pressed.
            self.worker.pad_deadline = time.monotonic() + .4
            for slot, button in enumerate(faces):
                if button in pressed:
                    saved = self.saved_slots[slot]
                    if saved is None: raise ValueError(f'Position {slot+1} is not saved')
                    bounded(saved[1], *self.worker.endpoints, 'Saved target')
                    self.pad_preset = (saved[1], axis)
            if self.pad_preset and abs(axis - self.pad_preset[1]) > .15: self.pad_preset = None
            target = self.pad_preset[0] if self.pad_preset else stick_target(axis, self.worker.endpoints)
            if self.busy or self.worker.stop_now.is_set() or time.monotonic() < self.pad_next: return
            if target != self.pad_last or not self.worker.active:
                args = (target, bounded(self.current.get(), .1, 2., 'Current (A)'),
                        bounded(self.max_speed.get(), .1, 60., 'Position speed (RPM)'),
                        bounded(self.acceleration.get(), .1, 10., 'Acceleration'))
                self.submit('controller', args)
                self.pad_last = target; self.pad_next = time.monotonic() + .1
        except Exception as exc:
            self.disarm_pad(); self.pad_status.set('Controller stopped: ' + str(exc))

    def save_slot(self, slot, capture=False):
        try:
            if not self.worker.endpoints: raise ValueError('Enable endpoints and press Apply first.')
            value = None if capture else degree_value(self.slot_angles[slot].get())
            if value is not None: bounded(value, *self.worker.endpoints, 'Saved position')
            self.submit('save_position', (slot, self.slot_names[slot].get(), value))
        except ValueError as exc: messagebox.showerror('Saved position', str(exc))

    def move_slot(self, slot):
        try:
            if not self.worker.endpoints: raise ValueError('Enable endpoints and press Apply first.')
            saved = self.saved_slots[slot]
            if saved is None: raise ValueError('Save this position first.')
            bounded(saved[1], *self.worker.endpoints, 'Saved target')
            args = (slot, bounded(self.current.get(), .1, 2., 'Current (A)'),
                    bounded(self.max_speed.get(), .1, 60., 'Position speed (RPM)'),
                    bounded(self.acceleration.get(), .1, 10., 'Acceleration (rad/s²)'))
            self.submit('move_saved', args)
        except ValueError as exc: messagebox.showerror('Saved position', str(exc))

    def reverse_inputs(self):
        requested = self.reverse.get()
        self.reverse.set(self.worker.direction == -1)
        self.submit('reverse', (requested,))

    def add_sweep(self):
        try:
            if not self.connected: raise ValueError('Connect before planning a sweep.')
            if not self.worker.endpoints: raise ValueError('Apply endpoints before planning sweeps.')
            start = degree_value(self.sweep_start.get())
            end = degree_value(self.sweep_end.get())
            bounded(start, *self.worker.endpoints, 'Sweep start')
            bounded(end, *self.worker.endpoints, 'Sweep end')
            self.sweeps.append((start, end))
            self.sweep_start.set(str(start)); self.sweep_end.set(str(end))
            self.sweep_list.insert('end', f'{start}° → {end}° (preview only)')
            self.sweep_list.selection_clear(0, 'end')
            self.sweep_list.selection_set('end')
            self.draw_arc()
        except ValueError as exc: messagebox.showerror('Sweep preview', str(exc))

    def remove_sweep(self):
        selected = self.sweep_list.curselection()
        if selected:
            del self.sweeps[selected[0]]
            self.sweep_list.delete(selected[0]); self.draw_arc()

    def clear_sweeps(self):
        self.sweeps.clear(); self.sweep_list.delete(0, 'end')

    def draw_arc(self):
        if not hasattr(self, 'arc'): return
        c = self.arc; c.delete('all')
        cx, cy, radius = max(c.winfo_width(), 600) / 2, 115, 82
        def point(deg, r=radius):
            a = math.radians(deg)
            return cx + r * math.cos(a), cy - r * math.sin(a)
        def arc_path(start, end, color, width, r=radius):
            # Full turns overlap on a circle; signed labels retain turn information.
            end = start + max(-360, min(360, end - start))
            steps = max(2, int(abs(end - start) / 2))
            coords = [v for i in range(steps + 1) for v in point(start + (end-start)*i/steps, r)]
            c.create_line(*coords, fill=color, width=width)
        c.create_oval(cx-radius, cy-radius, cx+radius, cy+radius, outline='#aab3bd', width=2)
        limits = self.worker.endpoints
        if limits:
            arc_path(*limits, '#2e8b57', 10)
            for angle in limits:
                x,y = point(angle, radius+15)
                c.create_text(x,y,text=f'{angle}°',fill='#166534')
        c.create_text(12, 12, anchor='nw', text=('Applied limits: %d° to %d°' % limits) if limits else 'Endpoints OFF', fill='#166534' if limits else '#b4232c')
        c.create_text(12, 34, anchor='nw', text='Direction: ' + ('reversed' if self.worker.direction == -1 else 'normal'))
        selected = self.sweep_list.curselection() if hasattr(self, 'sweep_list') else ()
        if selected and selected[0] < len(self.sweeps):
            start,end = self.sweeps[selected[0]]
            valid = limits and limits[0] <= min(start,end) and max(start,end) <= limits[1]
            arc_path(start,end,'#9333ea' if valid else '#b4232c',4,radius-14)
            c.create_text(12, 56, anchor='nw', text=f'Sweep: {start}° → {end}°' + ('' if valid else ' — OUTSIDE LIMITS'))
        if self.position is not None:
            c.create_line(cx,cy,*point(self.position),fill='#2563eb',width=3)
            c.create_text(12, 78, anchor='nw', text=f'Measured: {self.position}°',fill='#2563eb')
        try:
            target = degree_value(self.angle.get())
            c.create_line(cx,cy,*point(target,radius-7),fill='#d97706',width=2,dash=(4,3))
            c.create_text(12,100,anchor='nw',text=f'Target field: {target}°',fill='#d97706')
        except ValueError: pass

    def capture(self, var):
        if self.position is not None:
            var.set(str(whole_degrees(self.position)))

    def apply_endpoints(self):
        try:
            limits = None
            if self.use_endpoints.get():
                limits = degree_limits(self.low.get(), self.high.get())
                self.low.set(str(limits[0])); self.high.set(str(limits[1]))
            self.submit('endpoints', (limits,))
        except ValueError as exc: messagebox.showerror('Endpoints', str(exc))

    def field(self, frame, label, value, col):
        frame.columnconfigure(col, weight=1)
        ttk.Label(frame, text=label).grid(row=0, column=col, sticky='w', padx=4)
        var = tk.StringVar(value=value)
        ttk.Entry(frame, textvariable=var, width=12).grid(row=1, column=col, sticky='w', padx=4, pady=5)
        return var

    def add_motion(self, parent, text, command):
        button = ttk.Button(parent, text=text, command=command)
        self.motion_buttons.append(button)
        return button

    def refresh(self):
        try:
            from serial.tools import list_ports
            self.ports['values'] = [p.device for p in list_ports.comports()]
        except ImportError: self.ports['values'] = ['COM3']

    def log_message(self, text):
        self.log.configure(state='normal')
        self.log.insert('end', time.strftime('%H:%M:%S  ') + text + '\n')
        if int(self.log.index('end-1c').split('.')[0]) > 150: self.log.delete('1.0', '30.0')
        self.log.see('end'); self.log.configure(state='disabled')

    def update_controls(self):
        enabled = self.connected and not self.busy and not self.closing and not self.pad_enabled.get()
        for button in self.motion_buttons + [self.zero_button, self.endpoint_button, self.reverse_button]: button.configure(state='normal' if enabled else 'disabled')
        for button in self.slot_save_buttons:
            button.configure(state='normal' if enabled and self.worker.endpoints else 'disabled')
        for i, button in enumerate(self.slot_move_buttons):
            saved = self.saved_slots[i]
            valid = enabled and saved is not None and self.worker.endpoints is not None
            if valid: valid = self.worker.endpoints[0] <= saved[1] <= self.worker.endpoints[1]
            button.configure(state='normal' if valid else 'disabled')
        self.connect_button.configure(text='Disconnect' if self.connected else 'Connect',
                                      state='disabled' if self.busy or self.closing else 'normal')
        for widget in (self.ports, self.id_entry):
            widget.configure(state='disabled' if self.connected or self.busy else 'normal')

    def submit(self, name, args=()):
        if self.busy or self.closing or self.worker.stop_now.is_set(): return
        if self.pad_enabled.get() and name != 'controller': self.disarm_pad()
        try: self.worker.commands.put_nowait((name, args))
        except queue.Full: return
        self.busy = True; self.update_controls()

    def connect(self):
        if self.connected: self.submit('disconnect'); return
        try:
            motor = int(self.motor.get())
            if not 0 <= motor <= 255 or motor == HOST: raise ValueError('Use an ID from 0–255, excluding 253.')
            if not self.port.get().strip(): raise ValueError('Choose a COM port.')
            self.submit('connect', (self.port.get().strip(), motor))
        except ValueError as exc: messagebox.showerror('Connection', str(exc))

    def move(self, mode, target):
        try:
            value = (degree_value(target, label='Target') if mode == 'position'
                     else bounded(target, -60, 60, 'Target'))
            if mode == 'position': self.angle.set(str(value))
            if mode == 'position' and self.worker.endpoints:
                bounded(value, *self.worker.endpoints, 'Target travel')
            if mode == 'speed' and value != 0 and self.worker.endpoints:
                raise ValueError('Turn off endpoints and Apply before continuous speed mode.')
            args = (mode, value, bounded(self.current.get(), .1, 2., 'Current (A)'),
                    bounded(self.max_speed.get(), .1, 60., 'Position speed (RPM)'),
                    bounded(self.acceleration.get(), .1, 10., 'Acceleration (rad/s²)'))
            self.submit('motion', args)
        except ValueError as exc: messagebox.showerror('Check the number', str(exc))

    def jog(self, direction):
        if self.position is None: return
        try:
            step = degree_value(self.step.get(), 1, 180, 'Jog step')
            self.step.set(str(step))
            value = whole_degrees(self.position) + direction * step
            if self.worker.endpoints:
                bounded(value, *self.worker.endpoints, 'Jog target')
            self.angle.set(str(value)); self.move('position', value)
        except ValueError as exc: messagebox.showerror('Jog limit', str(exc))

    def stop(self):
        self.disarm_pad()
        self.worker.stop_now.set()
        self.status.set('Disable requested…')

    def close(self):
        if self.closing: return
        self.disarm_pad()
        self.closing = True; self.worker.quit_now.set(); self.worker.stop_now.set()
        self.update_controls(); self.status.set('Disabling and closing…')

    def tick(self):
        self.worker.heartbeat = time.monotonic()
        while True:
            try: kind, value = self.worker.events.get_nowait()
            except queue.Empty: break
            if kind == 'readings':
                self.position = whole_degrees(value[0])
                self.travel.set(f'Total travel: {self.position}°')
                self.live[0].set(str(self.position % 360))
                for var, number in zip(self.live[1:], value[1:]): var.set(f'{number:.2f}')
            elif kind == 'saved_positions':
                self.saved_slots = list(value)
                for i, saved in enumerate(value):
                    if saved is not None:
                        self.slot_names[i].set(saved[0]); self.slot_angles[i].set(str(saved[1]))
                    self.slot_move_buttons[i].configure(text=(f'{saved[0]} → {saved[1]}°' if saved else f'Move {i+1} — not saved'))
            elif kind == 'saved_target':
                self.angle.set(str(value))
            elif kind == 'direction':
                direction, limits = value
                self.reverse.set(direction == -1)
                if limits:
                    self.low.set(str(limits[0])); self.high.set(str(limits[1]))
                self.angle.set('0'); self.position = None
                self.clear_sweeps()
            elif kind == 'reference':
                self.clear_sweeps()
                self.position = None
                self.angle.set('0')
                if value:
                    self.low.set(str(value[0])); self.high.set(str(value[1]))
                self.travel.set('Total travel: 0°')
                self.live[0].set('0')
            elif kind == 'connected':
                self.connected = value
                self.clear_sweeps()
                if value: self.angle.set('0')
                else:
                    self.position = None
                    self.travel.set('Total travel: —')
                    for var in self.live: var.set('—')
            elif kind == 'ready': self.busy = False
            elif kind == 'log': self.log_message(value)
            elif kind in ('status', 'error'):
                self.status.set(value); self.log_message(value)
                if kind == 'error':
                    self.disarm_pad()
                    self.pending_error = value
            elif kind == 'closed':
                # Keep any failed stop visible rather than silently closing.
                if 'UNCONFIRMED' in self.status.get():
                    messagebox.showwarning('Motor stop unconfirmed', self.status.get())
                self.root.destroy(); return
        self.poll_pad()
        self.update_controls()
        self.draw_arc()
        self.root.after(50, self.tick)
        if self.pending_error and not self.closing:
            error = self.pending_error
            self.pending_error = None
            messagebox.showerror('RS05 command failed', error, parent=self.root)


def self_test():
    for raw, expected in ((250.14, 250), (249.9, 250), (250.5, 251),
                          (-250.5, -251), (-.14, 0), (359.9, 360)):
        assert whole_degrees(raw) == expected
    assert whole_degrees(359.9) % 360 == 0
    assert whole_degrees(-1.1) % 360 == 359
    assert degree_limits(-29.8, 60.2) == (-30, 60)
    assert rebased_limits((-30, 60), -10.25) == (-40, 49)
    for invalid in ('nan', 'inf', '-inf'):
        try: degree_value(invalid)
        except ValueError: pass
        else: raise AssertionError('Non-finite angle accepted')
    try: degree_limits(.1, .2)
    except ValueError: pass
    else: raise AssertionError('Collapsed integer endpoints accepted')
    assert encode(0x1200FD01, bytes.fromhex('0570000001000000')).hex() == '41549007e80c0805700000010000000d0a'
    packet = encode(0x110001FD, b'AT\r\n1234')
    for i in range(len(packet) + 1):
        p = FrameParser()
        assert p.feed(b'noise' + packet[:i]) + p.feed(packet[i:]) == [(0x110001FD, b'AT\r\n1234')]
    for value in ('nan', 'inf', '-inf', '61'):
        try: bounded(value, -60, 60, 'speed')
        except ValueError: pass
        else: raise AssertionError('Invalid value accepted')
    w = Worker(demo=True)
    w.guard = lambda: None
    w.execute(('connect', ('COM3', 1)))
    serial_port = w.bus.serial
    assert not serial_port.enabled
    w.motion('position', 10, .5, 5, 1)
    assert serial_port.enabled and math.isclose(serial_port.params[0x7016], math.radians(10), abs_tol=1e-6)
    assert serial_port.params[0x7028] == 20000
    w.endpoints = None
    # Verify rounding reaches the transmitted target, not only the display.
    w.origin = .123456  # Keep a precise, non-integer-degree reference.
    for raw, expected in ((250.14, 250), (-25.7, -26), (375.2, 375)):
        w.motion('position', raw, .5, 5, 1)
        assert math.isclose(serial_port.params[0x7016],
                            w.origin + math.radians(expected), abs_tol=1e-6)
    w.stop_motor()
    serial_port.params[0x7019] = w.origin + math.radians(10.25)
    w.endpoints = (-30, 60)
    w.execute(('zero', ()))
    assert w.endpoints == (-40, 49) and not serial_port.enabled
    w.execute(('endpoints', ((-29.8, 60.2),)))
    assert w.endpoints == (-30, 60)
    try: w.motion('position', 60.6, .5, 5, 1)
    except ValueError: pass
    else: raise AssertionError('Rounded target bypassed endpoint')
    w.endpoints = None
    w.motion('speed', -5, .5, 5, 1)
    assert math.isclose(serial_port.params[0x700A], -5 * RPM, abs_tol=1e-6)
    w.stop_motor(); assert not serial_port.enabled
    w.stop_now.set()
    w.bus.guard = lambda: (_ for _ in ()).throw(Cancelled())
    try: w.motion('speed', 5, .5, 5, 1)
    except Cancelled: pass
    else: raise AssertionError('Stop cancellation ignored')
    assert not serial_port.enabled
    w.stop_now.clear()
    w.bus.guard = lambda: None
    original_read = w.bus.read
    w.bus.read = lambda index, fmt='f': 99 if index == 0x7005 else original_read(index, fmt)
    try: w.motion('speed', 5, .5, 5, 1)
    except RuntimeError: pass
    else: raise AssertionError('Mode mismatch ignored')
    assert not serial_port.enabled
    w.disconnect()
    r = Worker(demo=True)
    r.guard = lambda: None
    r.execute(('connect', ('COM3', 1)))
    port = r.bus.serial
    r.endpoints = (-30, 60)
    physical = sorted(r.origin + r.direction * math.radians(a) for a in r.endpoints)
    r.execute(('reverse', (True,)))
    assert r.direction == -1 and r.endpoints == (-60, 30) and not port.enabled
    assert physical == sorted(r.origin + r.direction * math.radians(a) for a in r.endpoints)
    r.motion('position', 25, .5, 5, 1)
    assert math.isclose(port.params[0x7016], r.origin - math.radians(25), abs_tol=1e-6)
    r.endpoints = None
    r.motion('speed', 5, .5, 5, 1)
    assert math.isclose(port.params[0x700A], -5 * RPM, abs_tol=1e-6)
    r.execute(('reverse', (False,)))
    assert r.direction == 1 and not port.enabled
    r.disconnect()
    p = Worker(demo=True)
    p.guard = lambda: None
    p.execute(('connect', ('COM3', 1)))
    port = p.bus.serial
    p.endpoints = (-170, 170)
    port.params[0x7019] = math.radians(-160)
    for slot, angle in enumerate((-160, -20, 40, 160)):
        p.execute(('save_position', (slot, f'Pose {slot + 1}', angle)))
    assert not port.enabled  # Saving never enables or moves.
    p.execute(('move_saved', (3, .5, 5, 1)))
    assert math.isclose(port.params[0x7016], math.radians(160), abs_tol=1e-6)
    # The requested path is -160 to +160, not the forbidden shortcut to -200.
    p.stop_motor()
    p.endpoints = (-30, 60)
    try: p.move_saved(3, .5, 5, 1)
    except ValueError: pass
    else: raise AssertionError('Saved target bypassed changed limits')
    assert not port.enabled
    port.params[0x7019] = math.radians(70)
    try: p.move_saved(2, .5, 5, 1)
    except ValueError: pass
    else: raise AssertionError('Outside current position accepted')
    assert not port.enabled
    p.endpoints = None
    try: p.move_saved(2, .5, 5, 1)
    except ValueError: pass
    else: raise AssertionError('Preset moved without limits')
    p.endpoints = (-170, 170)
    p.execute(('reverse', (True,)))
    assert p.saved_positions == [None] * 4
    p.save_position(0, 'Reversed', 20)
    p.move_saved(0, .5, 5, 1)
    assert math.isclose(port.params[0x7016], math.radians(-20), abs_tol=1e-6)
    p.execute(('zero', ()))
    assert p.saved_positions == [None] * 4
    p.save_position(0, 'New reference', 0)
    p.disconnect()
    assert stick_target(-1, (-30, 70)) == -30
    assert stick_target(1, (-30, 70)) == 70
    assert stick_target(.05, (-30, 70)) == 20
    assert stick_target(-.05, (-30, 70)) == 20
    targets = [stick_target(i / 100, (-30, 70)) for i in range(-100, 101)]
    assert targets == sorted(targets) and min(targets) >= -30 and max(targets) <= 70
    g = Worker(demo=True); g.guard = lambda: None
    g.execute(('connect', ('COM3', 1)))
    port = g.bus.serial
    g.controller_target(20, .5, 5, 1)
    enables = sum(kind == 3 for kind, data in port.history)
    g.controller_target(30, .5, 5, 1)
    assert sum(kind == 3 for kind, data in port.history) == enables
    assert math.isclose(port.params[0x7016], math.radians(30), abs_tol=1e-6)
    try: g.controller_target(181, .5, 5, 1)
    except ValueError: pass
    else: raise AssertionError('Controller bypassed endpoints')
    g.pad_deadline = time.monotonic() - 1
    g.heartbeat = time.monotonic()
    try: Worker.guard(g)
    except RuntimeError: pass
    else: raise AssertionError('Controller expiry ignored')
    g.stop_motor()
    assert not port.enabled and g.pad_deadline is None
    g.disconnect()
    p.execute(('connect', ('COM3', 1)))
    assert p.saved_positions == [None] * 4
    p.disconnect()
    print('PASS: framing, fragmented input, input limits, connect-disabled, position/speed commands,')
    print('input reversal, disable before reversal, physical endpoint preservation,')
    print('four saved positions, no wrapped shortcut, changed-limit/outside-current rejection, slot invalidation,')
    print('gamepad range/deadzone, continuous target updates, controller expiry,')
    print('whole-degree targets on CAN, wrapping, endpoint rounding/re-zero,')
    print('watchdog write, stop priority, readback rejection, disconnect. No real hardware tested.')


if __name__ == '__main__':
    if '--self-test' in sys.argv: self_test()
    else:
        root = tk.Tk()
        App(root, demo='--demo' in sys.argv)
        root.mainloop()
