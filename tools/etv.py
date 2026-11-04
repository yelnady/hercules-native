#!/usr/bin/env python3
"""Decoder for Disney Interactive's .ETV full-motion-video container (Hercules Action Game, 1997).

Format (all derived from decompiling HERCULES.EXE):
  header 0x348 bytes: u32 magic 'ETV\\n', width, height, frames, fps, channels, rate, bits,
                      two unknown u32, then from 0x28 a table of per-second chunk sizes (0-terminated)
  chunk (one second): [audio PCM: channels*bits/8*rate/fps*fps bytes] then `fps` frame records
  frame record:       u32 size | u8 flag | (flag? 768-byte palette + 8192-byte blend mask) |
                      u32 (unused) | RNC method-2 packed block data
  block data:         2-bit-per-4x4-block type map, then pixels:
                        type 0 = 16 raw bytes, type 2 = 1 fill byte, types 1/3 = keep previous frame
  type-2 blocks (not on the border) get a second pass that blends in a randomly offset neighbour
  pixel through a 256x256 table built from the 8192-byte mask.
"""
import struct, sys, os, zlib, wave

class RNCError(Exception): pass

def crc16(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

def rnc2_unpack(src, pos=0, check=True):
    if src[pos:pos+3] != b'RNC' or src[pos+3] != 2:
        raise RNCError('not RNC method 2')
    unp_len, pck_len = struct.unpack_from('>II', src, pos+4)
    unp_crc, pck_crc = struct.unpack_from('>HH', src, pos+12)
    inp = pos + 18
    end_in = inp + pck_len
    out = bytearray()
    st = {'p': inp, 'buf': 0, 'cnt': 0}
    def bit():
        if st['cnt'] == 0:
            st['buf'] = src[st['p']]; st['p'] += 1; st['cnt'] = 8
        b = (st['buf'] >> 7) & 1
        st['buf'] = (st['buf'] << 1) & 0xFF
        st['cnt'] -= 1
        return b
    def byte():
        v = src[st['p']]; st['p'] += 1; return v
    def length():
        a = bit()
        if bit() == 0: return a + 4
        c = bit()
        return c - 2 + (a + 4) * 2
    def dist():
        x = 0
        if bit():
            x = bit()
            if bit() == 0:
                if x == 0: x = bit() + 2
            else:
                x = (bit() + x * 2) | 4
                if bit() == 0: x = bit() + x * 2
        return x * 256 + 1 + byte()
    def copy(n, d):
        for _ in range(n): out.append(out[-d])
    bit(); bit()                                  # two header bits
    while len(out) < unp_len:
        if bit() == 0:
            out.append(byte()); continue
        if bit() == 0:
            n = length()
            if n == 9:
                k = (bit() << 3) | (bit() << 2) | (bit() << 1) | bit()
                for _ in range(k * 4 + 12): out.append(byte())
            else:
                copy(n, dist())
            continue
        if bit() == 0:
            copy(2, byte() + 1); continue
        if bit() == 0:
            copy(3, dist()); continue
        n = byte() + 8
        if n == 8:
            bit()                                  # end-of-chunk marker
            continue
        copy(n, dist())
    out = bytes(out[:unp_len])
    if check and crc16(out) != unp_crc:
        raise RNCError('unpacked CRC mismatch (%04x != %04x)' % (crc16(out), unp_crc))
    return out

def _crc_step(crc, b):
    crc ^= b
    for _ in range(8):
        crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

REPAIRS = []   # (description) of every single-bit repair applied while decoding

def rnc2_unpack_repair(src, pos, label=''):
    """rnc2_unpack, but if the data fails its CRC try to repair a single flipped bit.
    A repair is accepted only if it satisfies BOTH the packed-data CRC and the unpacked-data CRC."""
    try:
        return rnc2_unpack(src, pos)
    except RNCError:
        pass
    pck = struct.unpack_from('>I', src, pos+8)[0]
    want = struct.unpack_from('>H', src, pos+14)[0]
    seg = bytes(src[pos+18:pos+18+pck])
    diff = crc16(seg) ^ want
    if diff == 0:
        raise RNCError('packed data CRC ok but unpacked CRC wrong (decoder bug?)')
    delta = [_crc_step(0, 1 << k) for k in range(8)]
    cands = []
    for p in range(pck-1, -1, -1):
        for k in range(8):
            if delta[k] == diff: cands.append((p, k))
        delta = [_crc_step(x, 0) for x in delta]
    for p, k in cands:
        fixed = bytearray(src[pos:pos+18+pck]); fixed[18+p] ^= 1 << k
        try:
            out = rnc2_unpack(bytes(fixed))
        except Exception:
            continue
        REPAIRS.append('%s: flipped bit %d of byte 0x%x' % (label, k, pos+18+p))
        return out
    raise RNCError('unrecoverable corruption in %s' % label)

class MsvcRand:
    def __init__(self, seed=1): self.s = seed & 0xFFFFFFFF
    def __call__(self):
        self.s = (self.s * 0x343FD + 0x269EC3) & 0xFFFFFFFF
        return (self.s >> 16) & 0x7FFF

class ETV:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        (self.magic, self.w, self.h, self.nframes, self.fps, self.channels,
         self.rate, self.bits) = struct.unpack_from('<8I', self.d, 0)
        if self.magic != 0x0A565445: raise ValueError('not an ETV file')
        self.chunks = []
        i = 0x28
        while struct.unpack_from('<I', self.d, i)[0]:
            self.chunks.append(struct.unpack_from('<I', self.d, i)[0]); i += 4
        self.audio_per_chunk = (self.channels * self.bits // 8) * self.rate // self.fps * self.fps

    def audio(self):
        out = bytearray(); off = 0x348
        for sz in self.chunks:
            out += self.d[off:off+self.audio_per_chunk]; off += sz
        return bytes(out)

    def frames(self, seed=1):
        """Yield (index, width, height, palette[256 x (r,g,b) 8-bit], pixels bytes)."""
        w, h = self.w, self.h
        frame = bytearray(w*h); ref = bytearray(w*h)
        rnd = MsvcRand(seed)
        offs = []
        for _ in range(47):                      # same table the game builds in FUN_004020c0
            a = (rnd() % 5 - 2) * w
            offs.append(a - 2 + rnd() % 5)
        lut = None; pal = None; widx = 0; n = 0
        off = 0x348
        for sz in self.chunks:
            p = off + self.audio_per_chunk; end = off + sz
            while p < end:
                fs = struct.unpack_from('<I', self.d, p)[0]; flag = self.d[p+4]; q = p + 5
                if flag:
                    raw = self.d[q:q+768]
                    scale = 4 if max(raw) < 64 else 1          # 6-bit VGA palette vs 8-bit
                    pal = [(raw[i]*scale if scale == 1 else min(255, raw[i]*scale+(raw[i]>>4)),
                            raw[i+1]*scale if scale == 1 else min(255, raw[i+1]*scale+(raw[i+1]>>4)),
                            raw[i+2]*scale if scale == 1 else min(255, raw[i+2]*scale+(raw[i+2]>>4)))
                           for i in range(0, 768, 3)]
                    mask = self.d[q+768:q+768+8192]
                    lut = bytearray(65536)
                    for row in range(256):
                        for col in range(256):
                            lut[row*256+col] = col if mask[row*32+(col>>3)] >> (col & 7) & 1 else row
                    q += 0x2300
                data = rnc2_unpack_repair(self.d, q + 4, 'frame %d' % n)
                nblk = (w*h)//16
                tmap = data[:nblk//4]; px = (w*h)//64
                # pass 1
                for by in range(0, h, 4):
                    for bx in range(0, w, 4):
                        bi = (bx >> 4) + (w >> 4) * (by >> 2)
                        t = (tmap[bi] >> ((bx >> 1) & 6)) & 3
                        if t == 0:
                            for r in range(4):
                                a = (by+r)*w + bx
                                frame[a:a+4] = data[px:px+4]; ref[a:a+4] = data[px:px+4]; px += 4
                        elif t == 2:
                            v = data[px]; px += 1
                            for r in range(4):
                                a = (by+r)*w + bx
                                frame[a:a+4] = bytes([v])*4; ref[a:a+4] = bytes([v])*4
                # pass 2: jittered neighbour blend on interior fill blocks
                if lut is not None:
                    for by in range(4, h-4, 4):
                        for bx in range(4, w-4, 4):
                            bi = (bx >> 4) + (w >> 4) * (by >> 2)
                            if (tmap[bi] >> ((bx >> 1) & 6)) & 3 != 2: continue
                            for r in range(4):
                                for c in range(4):
                                    widx += 1
                                    if widx > 0x2e: widx = 0
                                    a = (by+r)*w + bx + c
                                    nb = a + offs[widx]
                                    if 0 <= nb < w*h:
                                        frame[a] = lut[frame[a]*256 + ref[nb]]
                yield n, w, h, pal, bytes(frame)
                n += 1; p += fs
            off = end

def write_png(path, w, h, pal, px):
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t+b) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + px[y*w:(y+1)*w] for y in range(h))
    plte = b''.join(bytes(c) for c in pal)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0))
                + chunk(b'PLTE', plte) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def write_wav(path, e):
    with wave.open(path, 'wb') as w:
        w.setnchannels(e.channels); w.setsampwidth(e.bits//8); w.setframerate(e.rate); w.writeframes(e.audio())

if __name__ == '__main__':
    src, outdir = sys.argv[1], sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else None
    os.makedirs(outdir, exist_ok=True)
    e = ETV(src)
    print('%dx%d, %d frames @ %d fps, audio %d Hz %d-bit x%d' % (e.w, e.h, e.nframes, e.fps, e.rate, e.bits, e.channels))
    write_wav(os.path.join(outdir, 'audio.wav'), e)
    cnt = 0
    for i, w, h, pal, px in e.frames():
        write_png(os.path.join(outdir, 'frame_%05d.png' % i), w, h, pal, px); cnt += 1
        if limit and cnt >= limit: break
    print('wrote', cnt, 'frames')
