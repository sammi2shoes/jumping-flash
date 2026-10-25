#!/usr/bin/env python3
"""Make a small static-analysis bundle from an AX12 APK. Standard library only.

Reads APK as ZIP; never installs, executes, patches, or extracts its code.
Usage: py prepare_ax12_apk.py RadioMasterAX.260908.apk
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

LIMIT = 28 * 1024 * 1024


def selected_names(names):
    abis = {n.split('/')[1] for n in names if n.startswith('lib/') and len(n.split('/')) == 3}
    abi = next((a for a in ('arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86') if a in abis), None)
    result = []
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or name.endswith('/'):
            continue
        base = path.name
        native = (abi and name.startswith('lib/' + abi + '/') and name.endswith('.so')
                  and not base.startswith(('libQt', 'libplugins_', 'libqml_'))
                  and base != 'libc++_shared.so')
        code = name in ('AndroidManifest.xml', 'resources.arsc') or re.fullmatch(r'classes\d*\.dex', name)
        script = name.startswith('assets/') and path.suffix.lower() in ('.lua', '.qml', '.qrc', '.rcc', '.json', '.xml', '.ini', '.conf')
        certificate = name.startswith('META-INF/') and path.suffix.upper() in ('.RSA', '.DSA', '.EC')
        if native or code or script or certificate:
            result.append(name)
    return sorted(result), abi


def prepare(apk, output):
    apk = Path(apk)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    with apk.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    bundle = output / 'AX12-analysis.zip'
    with zipfile.ZipFile(apk) as source:
        names, abi = selected_names(source.namelist())
        report = {'source_apk': apk.name, 'apk_sha256': digest.hexdigest(), 'selected_abi': abi,
                  'note': 'Repacked analysis files; not an installable APK. Repacking does not preserve APK v2/v3 signature verification.',
                  'selected': [], 'full_apk_index': [{'name': i.filename, 'bytes': i.file_size, 'compressed_bytes': i.compress_size} for i in source.infolist()]}
        with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as target:
            for name in names:
                data = source.read(name)
                target.writestr(name, data)
                report['selected'].append({'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
            target.writestr('AX12-ANALYSIS-INDEX.json', json.dumps(report, indent=2))
    (output / 'AX12-ANALYSIS-INDEX.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Selected ABI:', abi or 'none')
    print('Bundled', len(names), 'files; source APK unchanged.')
    if bundle.stat().st_size <= LIMIT:
        uploads = [bundle]
    else:
        uploads = []
        with bundle.open('rb') as source:
            index = 1
            while True:
                data = source.read(LIMIT)
                if not data:
                    break
                part = output / ('AX12-analysis.zip.part%02d' % index)
                part.write_bytes(data)
                uploads.append(part)
                index += 1
    print('\nUpload these files to the chat:')
    for path in uploads:
        print(' ', path.resolve(), '(%0.2f MiB)' % (path.stat().st_size / 1024 / 1024))
    if len(uploads) > 1:
        print('Upload ALL numbered parts, not the oversized ZIP. The parts will be rejoined for analysis.')
    return uploads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        prepare(args.apk, args.output or args.apk.parent / 'AX12-analysis')
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        parser.exit(1, 'Could not prepare APK: %s\n' % error)


if __name__ == '__main__':
    main()
