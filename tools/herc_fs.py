#!/usr/bin/env python3
"""HERCULES.FS archive: directory of {key u32, offset u32, size u32} (0x3000 bytes) + data.
Keys are a hash of the normalized, uppercased path (ported from FUN_00404e10 / FUN_00404fe0)."""
import struct, os

def s32(x): return ((x + 0x80000000) & 0xFFFFFFFF) - 0x80000000

def normalize(path):
    p = path.upper()
    if len(p) < 2 or p[1] != ':':
        p = 'M:' + (p if p.startswith('\\') else '\\' + p)
    drive, rest = p[0], p[2:]
    if drive == 'M': return p
    if drive == 'O': return 'M:\\GRAFIX\\MAPS' + rest
    if drive == 'P': return 'M:\\GRAFIX\\CHOP' + rest
    if drive in 'QR': return 'M:\\LANGUAGE\\SONIX' + rest
    i = rest.find('\\', 1)            # other drives: drop the first directory
    return 'M:' + (rest[i:] if i >= 0 else '')

def fs_hash(path):
    s = normalize(path).encode('latin1')
    h = 0; sh = 0
    for c in s:
        if c >= 0x80: c -= 0x100        # signed char
        h = (h + (c << sh)) & 0xFFFFFFFF
        sh += 8
        if sh > 0x18: sh = 0
    return (h + len(s)) & 0xFFFFFFFF

class FS:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        self.entries = []
        for i in range(0x3000 // 12):
            k, o, s = struct.unpack_from('<III', self.d, i*12)
            if k == 0: break
            self.entries.append((k, o, s))
        self.by_key = {k: (o, s) for k, o, s in self.entries}
    def get(self, name):
        k = fs_hash(name)
        if k not in self.by_key: return None
        o, s = self.by_key[k]; return self.d[o:o+s]
