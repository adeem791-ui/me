#!/usr/bin/env python3
"""Read-only structural inspector for Ghost of Yōtei's KCAP shader store.

Does not extract complete shaders, compile code, or modify the game.
Verified against the supplied PPSA26344 v01.008 all_shaders.xpps.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import struct
import sys

KCAP_HEADER_SECTION_COUNT_OFFSET = 0x68
KCAP_SECTION_TABLE_OFFSET = 0xE0
KCAP_SECTION_RECORD_BYTES = 12
KCAP_SECTION_COUNT_LIMIT = 65536
GATHER_WINDOW = bytes.fromhex('05 9f 94 f1 01 01 07 00 02 16 17 11 0e 23 00 00')


def sha256(blob):
    return hashlib.sha256(blob).hexdigest()


def read_xpps(path):
    data = path.read_bytes()
    if len(data) < 0xE0 or data[:4] != b'KCAP':
        raise ValueError('Not a KCAP .xpps header')
    count = struct.unpack_from('<I', data, KCAP_HEADER_SECTION_COUNT_OFFSET)[0]
    if count <= 0 or count > KCAP_SECTION_COUNT_LIMIT:
        raise ValueError(f'Unreasonable KCAP section count: {count}')
    header_end = KCAP_SECTION_TABLE_OFFSET + KCAP_SECTION_RECORD_BYTES * count
    if header_end > len(data):
        raise ValueError('KCAP section table exceeds file bounds')
    sections = []
    for i in range(count):
        size, offset, flags = struct.unpack_from('<III', data, KCAP_SECTION_TABLE_OFFSET + i * KCAP_SECTION_RECORD_BYTES)
        if size and (offset >= len(data) or size > len(data) - offset):
            raise ValueError(f'Section {i} is outside the file: {offset:x}+{size:x}')
        sections.append({'index': i, 'offset': offset, 'size': size, 'flags': f'0x{flags:08x}'})

    nonempty = [s for s in sections if s['size']]
    nonoverlap = all(nonempty[i]['offset'] + nonempty[i]['size'] <= nonempty[i+1]['offset'] for i in range(len(nonempty)-1))
    largest = max(nonempty, key=lambda s: s['size'])
    match_offsets = []
    start = 0
    while True:
        hit = data.find(GATHER_WINDOW, start)
        if hit < 0:
            break
        match_offsets.append(hit)
        start = hit + 1
    hits_in_largest = [x for x in match_offsets if largest['offset'] <= x < largest['offset'] + largest['size']]
    flags_counts = collections.Counter(s['flags'] for s in sections)
    return {
        'filename': path.name, 'size_bytes': len(data), 'sha256': sha256(data),
        'magic': 'KCAP', 'header_layout': 'PPSA26344 v01.008 observed layout (not a generic KCAP parser)',
        'section_count': count, 'section_table_offset': KCAP_SECTION_TABLE_OFFSET,
        'all_sections_in_bounds': True, 'sections_nonoverlapping_in_table_order': nonoverlap,
        'section_types': dict(flags_counts.most_common()),
        'largest_section': largest,
        'independent_gpu_instruction_witness': {
            'source': 'AnyPS5 GitHub issue #1941, published MIMG instruction window',
            'matches': len(match_offsets), 'file_offsets': match_offsets[:16],
            'inside_largest_section': len(hits_in_largest),
            'interpretation': 'The archive contains at least some GPU machine code; this is not a complete shader index.'
        },
        'footer_end_tag_bytes': data[-8:-4].hex(),
        'footer_end_tag_observed': data[-8:-4] == b' DNE',
        'limitations': 'Section offsets do NOT identify complete shader boundaries, resource specialization, shader stage, or Vulkan pipeline state.'
    }


def read_manifest(path):
    data = path.read_bytes()
    if len(data) < 40 or data[:4] != b'FNMS':
        raise ValueError('Not an FNMS .texmeshman header')
    listed = struct.unpack_from('<I', data, 0x1C)[0]
    names = []
    for hit in re.finditer(rb'[A-Za-z0-9_./-]{6,96}\.0\.sps', data[:8192]):
        candidate = hit.group(0).decode('ascii', errors='ignore')
        if candidate not in names:
            names.append(candidate)
    return {'filename': path.name,'size_bytes': len(data), 'sha256': sha256(data),
            'magic': 'FNMS', 'header_count_field': listed,
            'texture_resource_names_near_header': names[:30],
            'limitations': 'No confirmed shader offsets or shader-entry schema; contains texture-named metadata.'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('xpps', type=pathlib.Path, help='Path to all_shaders.xpps')
    p.add_argument('texmeshman', type=pathlib.Path, help='Path to all_shaders.texmeshman')
    p.add_argument('-o', '--output', type=pathlib.Path, default=pathlib.Path('Yotei_Shader_Inspection.json'))
    args = p.parse_args()
    try:
        report = {'tool': 'Yotei read-only shader container inspector v0.1',
                  'xpps': read_xpps(args.xpps), 'texmeshman': read_manifest(args.texmeshman),
                  'next_step': 'Reverse-engineer per-shader record boundaries + dynamic resource and pipeline-specialization metadata before offline compilation.'}
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    except (OSError, ValueError, struct.error) as err:
        print(f'Inspection failed: {err}', file=sys.stderr)
        return 1
    print(f'Validated {report["xpps"]["section_count"]} KCAP section entries.')
    print(f'Largest section: {report["xpps"]["largest_section"]["size"]:,} bytes')
    print(f'Independent shader instruction evidence: {report["xpps"]["independent_gpu_instruction_witness"]["matches"]} match(es)')
    print(f'Metadata report written: {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
