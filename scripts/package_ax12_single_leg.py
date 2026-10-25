#!/usr/bin/env python3
"""Package a built AX12SingleLeg image and self-contained Arduino sketch."""
import hashlib
from pathlib import Path
import subprocess
import sys
import zipfile

root=Path(__file__).resolve().parent.parent
build=root/'.pio/build/ax12-single-leg'
release=root/'releases'
name='AX12SingleLeg-v0.1'
# esptool is installed in scripts/setup.py's Python environment.
subprocess.run([sys.executable,'-m','esptool','--chip','esp32','merge_bin',
 '-o',str(release/(name+'-factory.bin')),'--flash_mode','dio','--flash_freq','40m','--flash_size','4MB',
 '0x1000',str(build/'bootloader.bin'),'0x8000',str(build/'partitions.bin'),
 '0xe000',str(Path(__import__('os').environ.get('JUMPING_FLASH_TOOLS','/workspace/tooling'))/'arduino-esp32-official/tools/partitions/boot_app0.bin'),
 '0x10000',str(build/'firmware.bin')],check=True)
source='#define AX12_BLE 1\n'+(root/'firmware/MotorRecovery/MotorRecovery.ino').read_text().replace('../AX12SingleLeg/AX12Transport.h','AX12Transport.h')
with zipfile.ZipFile(release/(name+'.zip'),'w',zipfile.ZIP_DEFLATED) as z:
 z.writestr('AX12SingleLeg/AX12SingleLeg.ino',source)
 for h in ['AX12Transport.h','AX12Protocol.h']:
  z.write(root/'firmware/AX12SingleLeg'/h,'AX12SingleLeg/'+h)
 z.write(release/(name+'-factory.bin'),name+'-factory.bin')
 z.write(root/'docs/AX12-SINGLE-LEG.md','README.md')
 z.writestr('FLASH.txt','In this extracted folder, close Serial Monitor and run:\npy -m pip install esptool\npy -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x0 AX12SingleLeg-v0.1-factory.bin\n\nUse the Stick COM port. See README.md for pairing, native input and manual reference. No motor enable on boot.\n')
for p in [release/(name+'.zip'),release/(name+'-factory.bin')]:
 p.with_name(p.name+'.sha256').write_text(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n')
print('Packaged',name)
