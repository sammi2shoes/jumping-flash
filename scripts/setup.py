#!/usr/bin/env python3
"""Bootstrap verified upstream tools on Linux x86_64 without the PIO registry."""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
import venv
import zipfile

TOOLS = Path(os.environ.get('JUMPING_FLASH_TOOLS', '/workspace/tooling')).resolve()

def run(*args):
    subprocess.run(args, check=True)

def archive(url, digest, destination, prefix, manifest=None):
    target = TOOLS / destination
    marker = target / '.verified-source'
    if marker.exists() and marker.read_text() == digest:
        return
    if target.exists():
        raise SystemExit(f'{target} exists without the expected verification marker; preserve/move it before setup')
    with tempfile.TemporaryDirectory() as tmp:
        blob = Path(tmp) / 'download'
        run('curl', '-fL', '--retry', '2', url, '-o', str(blob))
        if hashlib.sha256(blob.read_bytes()).hexdigest() != digest:
            raise SystemExit(f'Checksum mismatch: {url}')
        unpacked = Path(tmp) / 'unpacked'
        unpacked.mkdir()
        if zipfile.is_zipfile(blob):
            with zipfile.ZipFile(blob) as z:
                z.extractall(unpacked)
        else:
            with tarfile.open(blob) as t:
                t.extractall(unpacked, filter='data')
        shutil.move(str(unpacked / prefix), target)
    if manifest:
        (target / 'package.json').write_text(json.dumps(manifest))
    marker.write_text(digest)

def main():
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise SystemExit('This bootstrap targets Linux x86_64. See README for Arduino IDE setup on other hosts.')
    TOOLS.mkdir(parents=True, exist_ok=True)
    env = TOOLS / 'venv'
    if not (env / 'bin/python').exists():
        venv.create(env, with_pip=True)
    run(str(env / 'bin/python'), '-m', 'pip', 'install', 'platformio==6.1.18', 'scons==4.8.1', 'esptool==4.5.1')
    archive('https://github.com/ricardoquesada/esp32-arduino-lib-builder/releases/download/4.1.0/esp32-bluepad32-4.1.0.zip',
            '2b4916baba6c8d4e40b0330e12dd6a21ca7b95540337e6c2d2713831ccf3c394',
            'bluepad32', 'esp32-bluepad32-4.1.0', {'name':'framework-arduinoespressif32','version':'4.0.2'})
    archive('https://github.com/espressif/crosstool-NG/releases/download/esp-2021r2-patch5/xtensa-esp32-elf-gcc8_4_0-esp-2021r2-patch5-linux-amd64.tar.gz',
            '8ef14e0409c2011b41e504a30f70d3e35287313a795d1f2462ad2cd0e2052d37',
            'xtensa-esp32-elf', 'xtensa-esp32-elf', {'name':'toolchain-xtensa-esp32','version':'8.4.0+2021r2-patch5'})
    archive('https://github.com/espressif/arduino-esp32/releases/download/2.0.7/esptool-v4.5.1-src.tar.gz',
            'aa06831a7d88d8ccde4ea21241e983a08dbdae967290e181658b0d18bffc8f86',
            'esptool', 'esptool', {'name':'tool-esptoolpy','version':'1.40501.0'})
    # Adapt the upstream PyPI SCons distribution to PlatformIO's package layout.
    scons = TOOLS / 'platformio/packages/tool-scons'
    scons.mkdir(parents=True, exist_ok=True)
    (scons / 'scons.py').write_text('import SCons.Script\nSCons.Script.main()\n')
    (scons / 'package.json').write_text(json.dumps({'name':'tool-scons','version':'4.40801.0'}))
    (scons / '.piopm').write_text(json.dumps({'type':'tool','name':'tool-scons','version':'4.40801.0','spec':{'owner':'platformio','name':'tool-scons','requirements':'~4.40801.0'}}))
    print('Tool installation complete. Run scripts/build.sh to compile both targets.')

if __name__ == '__main__':
    main()
