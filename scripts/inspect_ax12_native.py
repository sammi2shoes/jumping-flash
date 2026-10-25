#!/usr/bin/env python3
"""Static AArch64/Qt resource inspection; never loads or executes the target ELF.

pip install -r scripts/ax12-analysis-requirements.txt
python scripts/inspect_ax12_native.py path/to/libRadioMasterAX_arm64-v8a.so --output analysis
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zlib
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.elf.elffile import ELFFile

FUNCTIONS = ('RcSrcGetRaw', 'getSrcCfgList', 'getValueiPb', 'luaGetValueAndPush',
             'luaFindFieldByName', 'rcJoystickMap', 'openComByName',
             'umbusDataPackRxed', 'AppRadioControl17umbusPackReceived')


def inspect(library, output):
    output.mkdir(parents=True, exist_ok=True)
    with library.open('rb') as stream:
        elf = ELFFile(stream)
        if elf['e_machine'] != 'EM_AARCH64':
            raise ValueError('This inspector supports AArch64 ELF only')
        segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        symbols = {s.name: s for s in elf.get_section_by_name('.dynsym').iter_symbols()}
        decoder = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)

        def read(address, size):
            if size < 0 or size > 64 * 1024 * 1024:
                raise ValueError('Read too large')
            for segment in segments:
                if segment['p_vaddr'] <= address and address + size <= segment['p_vaddr'] + segment['p_filesz']:
                    stream.seek(segment['p_offset'] + address - segment['p_vaddr'])
                    data = stream.read(size)
                    if len(data) == size:
                        return data
            raise ValueError('Unmapped file-backed read: %x + %x' % (address, size))

        targets = {s['st_value']: name for name, s in symbols.items() if s['st_info']['type'] == 'STT_FUNC' and s['st_value']}
        reloc = elf.get_section_by_name('.rela.plt')
        plt = elf.get_section_by_name('.plt')
        if reloc is not None and plt is not None:
            dynamic = elf.get_section(reloc['sh_link'])
            for number, r in enumerate(reloc.iter_relocations()):
                targets[plt['sh_addr'] + 32 + number * 16] = dynamic.get_symbol(r['r_info_sym']).name
        report = {'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(), 'functions': {}, 'resources': [], 'errors': []}
        with (output / 'native-functions.asm').open('w', encoding='utf-8') as assembly:
            for name, symbol in symbols.items():
                if symbol['st_info']['type'] != 'STT_FUNC' or not symbol['st_size'] or not any(q in name for q in FUNCTIONS):
                    continue
                address, size = symbol['st_value'], symbol['st_size']
                report['functions'][name] = {'address': hex(address), 'bytes': size}
                assembly.write('\n%s at %x (%d bytes)\n' % (name, address, size))
                for i in decoder.disasm(read(address, size), address):
                    extra = targets.get(int(i.op_str[1:], 0), '') if i.mnemonic in ('b', 'bl') and i.op_str.startswith('#') else ''
                    assembly.write('%x: %-8s %-35s %s\n' % (i.address, i.mnemonic, i.op_str, extra))

        # This build's qInitResources wrappers load tree/names/data into x1/x2/x3.
        for init, symbol in symbols.items():
            if 'qInitResources_' not in init or symbol['st_size'] != 52:
                continue
            registers = {}
            version = None
            for i in decoder.disasm(read(symbol['st_value'], 52), symbol['st_value']):
                operands = i.op_str.split(', ')
                if i.mnemonic == 'adrp':
                    registers[operands[0]] = int(operands[1].lstrip('#'), 0)
                elif i.mnemonic == 'add' and len(operands) == 3 and operands[2].startswith('#') and operands[1] in registers:
                    registers[operands[0]] = registers[operands[1]] + int(operands[2][1:], 0)
                elif version is None and i.mnemonic == 'mov' and operands[0] == 'w0' and operands[1].startswith('#'):
                    version = int(operands[1][1:], 0)
            if version != 3 or not all(r in registers for r in ('x1', 'x2', 'x3')):
                continue
            tree, names, data = (registers[r] for r in ('x1', 'x2', 'x3'))
            visited = set()

            def visit(node, parent):
                if node in visited or len(visited) >= 10000:
                    raise ValueError('Invalid/cyclic resource tree')
                visited.add(node)
                entry = read(tree + node * 22, 22)
                name_offset, flags = struct.unpack('>IH', entry[:6])
                length = struct.unpack('>H', read(names + name_offset, 2))[0]
                if length > 4096:
                    raise ValueError('Invalid resource name')
                name = read(names + name_offset + 6, length * 2).decode('utf-16be') if node else ''
                path = parent / name
                if path.is_absolute() or '..' in path.parts or '\\' in str(path):
                    raise ValueError('Unsafe resource path')
                if flags & 2:
                    count, first = struct.unpack('>II', entry[6:14])
                    if count > 10000:
                        raise ValueError('Invalid resource child count')
                    for child in range(first, first + count):
                        visit(child, path)
                else:
                    offset = struct.unpack('>I', entry[10:14])[0]
                    size = struct.unpack('>I', read(data + offset, 4))[0]
                    report['resources'].append({'init': init, 'path': str(path), 'stored_bytes': size, 'flags': flags})
                    if not str(path).endswith(('.qml', '.md', '.lua', '.tar.gz', '.qrc')):
                        return
                    blob = read(data + offset + 4, size)
                    if flags & 4:
                        raise ValueError('Zstd-compressed resources not supported')
                    if flags & 1:
                        expected = struct.unpack('>I', blob[:4])[0]
                        if expected > 64 * 1024 * 1024:
                            raise ValueError('Decompressed resource too large')
                        inflater = zlib.decompressobj()
                        blob = inflater.decompress(blob[4:], expected + 1)
                        if len(blob) != expected or not inflater.eof:
                            raise ValueError('Resource decompression length mismatch')
                    dest = output / 'resources' / init / path
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(blob)
            try:
                visit(0, PurePosixPath())
            except (ValueError, struct.error, zlib.error) as error:
                report['errors'].append({'init': init, 'error': str(error)})
        (output / 'native-index.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print('Indexed %d resource entries and %d functions; %d resource errors.' % (len(report['resources']), len(report['functions']), len(report['errors'])))
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    inspect(args.library, args.output)


if __name__ == '__main__':
    main()
