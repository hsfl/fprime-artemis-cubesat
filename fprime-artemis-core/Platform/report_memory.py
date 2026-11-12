#!/usr/bin/env python3
"""Audit a linked Teensy twin ELF; static reservations are not runtime free heap."""
import argparse
import hashlib
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf', type=Path)
args = parser.parse_args()
with args.elf.open('rb') as stream:
    elf = ELFFile(stream)
    table = elf.get_section_by_name('.symtab')
    def symbol(name):
        entries = table.get_symbol_by_name(name) or []
        if len(entries) != 1:
            raise SystemExit(f'Expected exactly one {name}, found {len(entries)}')
        return entries[0]
    cache = symbol('artemis_product_cache')
    section = elf.get_section(cache['st_shndx'])
    end = symbol('_end')['st_value']
    assert cache['st_value'] == 0x20280000
    assert cache['st_size'] == 196608
    assert section['sh_type'] == 'SHT_NOBITS'
    assert section['sh_size'] == cache['st_size']
    assert 0x20200000 <= end <= 0x20280000
    main = end - 0x20200000
    print(json.dumps({
        'elf_sha256': hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        'main_ram_static_high_water_bytes': main,
        'main_ram_capacity_bytes': 524288,
        'main_ram_static_percent': round(100 * main / 524288, 4),
        'main_malloc_address_space_remaining_bytes': 524288 - main,
        'cache_address': hex(cache['st_value']),
        'cache_noload_bytes': cache['st_size'],
        'cache_bank_bytes': 262144,
        'cache_bank_percent': 75.0,
        'combined_static_reservations_bytes': main + cache['st_size'],
        'physical_ram_bytes': 1048576,
        'combined_static_percent_of_physical': round(100 * (main + cache['st_size']) / 1048576, 4),
        'runtime_heap_stack_peaks': 'not measured; physical bench gate',
    }, indent=2))
