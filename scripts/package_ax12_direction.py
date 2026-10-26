#!/usr/bin/env python3
"""Package an app-only update preserving the existing AX12SingleLeg partition/NVS layout."""
from pathlib import Path
import hashlib
import shutil
import zipfile

root=Path(__file__).resolve().parent.parent
build=root/'.pio/build/ax12-direction-test'
releases=root/'releases'
name='AX12DirectionTest-v0.1'
partition=(build/'partitions.bin').read_bytes()
old=(releases/'AX12SingleLeg-v0.1-factory.bin').read_bytes()
if partition!=old[0x8000:0x8000+len(partition)]:
    raise SystemExit('Partition table differs: app-only update is not compatible')
app=releases/(name+'-app.bin')
shutil.copyfile(build/'firmware.bin',app)
source=(root/'firmware/AX12DirectionTest/AX12DirectionTest.ino').read_text().replace('../AX12SingleLeg/AX12Transport.h','AX12Transport.h')
with zipfile.ZipFile(releases/(name+'.zip'),'w',zipfile.ZIP_DEFLATED) as z:
    z.writestr('AX12DirectionTest/AX12DirectionTest.ino',source)
    for header in ['AX12Protocol.h','AX12Transport.h']:
        z.write(root/'firmware/AX12SingleLeg'/header,'AX12DirectionTest/'+header)
    z.write(app,app.name)
    z.write(root/'docs/AX12-DIRECTION-TEST.md','README.md')
    z.writestr('FLASH.txt','Both motor batteries OFF. Close Serial Monitor. From this extracted folder, replace COM3 with the Stick port:\npy -3.13 -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x10000 AX12DirectionTest-v0.1-app.bin\n\nAPP-ONLY update for an existing AX12SingleLeg v0.1 installation. NOT offset0x0. No zero/reference step. Install APK v0.6.0 and see README.md.\n')
for path in [app,releases/(name+'.zip')]:
    path.with_name(path.name+'.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
print('Partition matches existing AX12SingleLeg. Packaged app-only update:',name)
