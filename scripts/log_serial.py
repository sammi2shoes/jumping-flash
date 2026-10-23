#!/usr/bin/env python3
"""Capture diagnostic CSV over USB. Install pyserial on the attached computer."""
import argparse
import csv
import sys
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('port', help='e.g. COM4 or /dev/ttyUSB0')
parser.add_argument('output', help='new CSV filename (existing files are preserved)')
args = parser.parse_args()
header = 'sample_us,imu_fresh,ax,ay,az,gx,gy,gz,pad_connected,pad_fresh,lx,ly,rx,ry,buttons,skipped_logs'
try:
    with open(args.output, 'x', newline='') as output, serial.Serial(args.port, 115200, timeout=1) as port:
        writer = csv.writer(output)
        writer.writerow(header.split(','))
        while True:
            line = port.readline().decode('utf-8', errors='replace').strip()
            fields = line.split(',')
            if len(fields) != 16 or not fields[0].isdigit():
                continue  # Ignore boot/debug messages and partial lines.
            try:
                [float(value) for value in fields]
            except ValueError:
                continue
            writer.writerow(fields)
            output.flush()
except KeyboardInterrupt:
    print('Capture stopped.', file=sys.stderr)
