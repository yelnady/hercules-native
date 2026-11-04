#!/usr/bin/env python3
"""Decoders for HERCULES.FS assets: ESF sound effects (IMA ADPCM) and TGA screens."""
import struct, wave, os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from herc_fs import FS
from etv import write_png

IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]

def esf_decode(blob):
    """Return (rate, is8bit, flags_byte0, pcm16 bytes). Ported from FUN_0040f3d0 / FUN_00402410."""
    magic, size, rate, flags = struct.unpack_from('<4I', blob, 0)
    if magic != 0x03465345: raise ValueError('not ESF')
    data = blob[16:16 + size // 4]
    idx = 0; pred = 0; out = bytearray()
    nsamp = size // 2
    for i in range(nsamp):
        b = data[i >> 1] if (i >> 1) < len(data) else 0
        code = (b >> 4) if (i & 1) == 0 else (b & 0xF)       # high nibble first
        step = IMA_STEP[idx]
        idx = max(0, min(88, idx + IMA_INDEX[code]))
        diff = step >> 3
        if code & 4: diff += step
        if code & 2: diff += step >> 1
        if code & 1: diff += step >> 2
        if code & 8: diff = -diff
        pred = max(-32768, min(32767, pred + diff))
        out += struct.pack('<h', pred)
    return rate & 0xFFFF, bool((flags >> 8) & 0xFF), flags & 0xFF, bytes(out)

def tga_decode(blob):
    """Uncompressed color-mapped TGA -> (w, h, palette[(r,g,b)], top-down 8-bit pixels)."""
    idl, cmt, it, cmo, cml, cmd, xo, yo, w, h, bpp, desc = struct.unpack_from('<BBBHHBHHHHBB', blob, 0)
    if it != 1 or cmd != 24 or bpp != 8: raise ValueError('unsupported TGA type %d/%d/%d' % (it, cmd, bpp))
    p = 18 + idl
    pal = [(0, 0, 0)] * 256
    for i in range(cml):
        bl, g, r = blob[p + i*3:p + i*3 + 3]; pal[cmo + i] = (r, g, bl)
    p += cml * 3
    px = blob[p:p + w*h]
    # NOTE: these files claim bottom-up origin (desc bit 5 clear) but the pixel rows are stored
    # top-down; flipping per the spec renders them upside down, so the flag is ignored.
    return w, h, pal, px

def extract_all(fs_path, names_json, outdir):
    fs = FS(fs_path); names = json.load(open(names_json))
    n_snd = n_img = 0
    for k, name in sorted(names.items(), key=lambda x: x[1]):
        blob = fs.by_key.get(int(k, 16))
        if not blob: continue
        o, s = blob; data = fs.d[o:o+s]
        rel = name[3:].replace('\\', '/')        # strip "M:\"
        if name.endswith('.ESF'):
            rate, is8, _, pcm = esf_decode(data)
            dst = os.path.join(outdir, 'sounds', os.path.splitext(os.path.basename(rel))[0] + '.wav')
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with wave.open(dst, 'wb') as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm)
            n_snd += 1
        elif name.endswith('.TGA'):
            w, h, pal, px = tga_decode(data)
            dst = os.path.join(outdir, 'screens', os.path.splitext(rel.split('SCREENS/')[-1])[0] + '.png')
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            write_png(dst, w, h, pal, px); n_img += 1
    return n_snd, n_img

if __name__ == '__main__':
    print(extract_all(sys.argv[1], sys.argv[2], sys.argv[3]))
