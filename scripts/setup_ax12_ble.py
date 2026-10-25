#!/usr/bin/env python3
"""Install the SHA256-verified official ESP32 core for the BLE peripheral build.
Run scripts/setup.py first for the shared compiler/PlatformIO dependencies.
"""
from setup import archive

archive('https://github.com/espressif/arduino-esp32/releases/download/2.0.17/esp32-2.0.17.zip',
        '1f8658d4b18a8001ce782142ad08164af2991d70b83a147c3437a6ee30a9b225',
        'arduino-esp32-official', 'esp32-2.0.17',
        {'name': 'framework-arduinoespressif32', 'version': '3.20017.0'})
print('Official ESP32 core ready. Run scripts/build.sh -e ax12-single-leg')
