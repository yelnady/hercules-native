#!/usr/bin/env python3
"""Eurocom 'i1.4' ANIMPSX.BIN sprite banks (Hercules PC). Ported from ENG1.DLL:
FUN_10003cd0 (frame lookup), FUN_10004ca0 (RLE + VRAM upload), FUN_10004010 (piece draw),
FUN_10005780 (palette)."""
import struct

def psx_color(c):
    r = (c & 31) * 255 // 31; g = ((c >> 5) & 31) * 255 // 31; b = ((c >> 10) & 31) * 255 // 31
    return (r, g, b)

class Bank:
    def __init__(self, data):
        self.d = data
        if data[:4] not in (b'i1.4',): raise ValueError('not an i1.4 bank: %r' % data[:4])
        self.count = struct.unpack_from('<H', data, 4)[0]
        self.base = 4                                  # entry offsets are relative to file+4
    def entry(self, i):
        return struct.unpack_from('<I', self.d, 4 + 4*i)[0] if 1 <= i <= self.count else 0
    def palette(self, idx):
        po = 4*self.count + 16 + 2*idx
        if po + 2 > len(self.d): return None
        x = struct.unpack_from('<H', self.d, po)[0]
        n = struct.unpack_from('<H', self.d, 4 + x)[0]
        src = 4 + x + 4
        if n == 0 or n > 256 or src + 2*n > len(self.d): return None
        cols = [struct.unpack_from('<H', self.d, src + 2*i)[0] for i in range(n)]
        pal = [None] * 256                             # None = transparent
        if n == 256:
            for i, c in enumerate(cols): pal[i] = psx_color(c)   # (raw 256: STP semantics ignored)
            pal[0] = None
        else:
            for i in range(1, n): pal[i] = psx_color(cols[i])
            for i in range(n, 256): pal[i] = pal[i - n]
        return pal
    def frame(self, i):
        """Return (pieces, palette_index, bbox) with pieces=[(x, y, w, h, pixels_bytes, src_w)] in pixels
        relative to the hotspot, or None."""
        e = self.entry(i)
        if not e: return None
        f = (e >> 8) + self.base
        d = self.d
        n = d[f]
        if n == 0: return None
        nhit = struct.unpack_from('b', d, f+1)[0]
        p = f + 2 + nhit*6                              # bbox(4) + foot(2), then descriptors
        bbox = struct.unpack_from('4b', d, p)
        footx, footy = struct.unpack_from('2b', d, p+4)
        desc = p + 6
        stream = desc + n*4
        s = stream
        pieces = []; last_img = None; last_w = 0
        for k in range(n):
            b0, b1 = d[desc + 4*k], d[desc + 4*k + 1]
            x, y = struct.unpack_from('2b', d, desc + 4*k + 2)
            if not b0 & 0x80:
                w0 = ((b0 & 3) + 1) * 8; h0 = (((b0 >> 2) & 3) + 1) * 8
                buf = bytearray()
                while True:
                    c = d[s]; s += 1
                    if c == 0 or c >= 0x80:
                        buf.append(c)
                    elif c == 2:
                        break
                    else:
                        buf += bytes([d[s]]) * c; s += 1
                buf = bytes(buf[:w0*h0]).ljust(w0*h0, b'\0')
                last_img, last_w, last_h = buf, w0, h0
                u = v = 0
            else:
                u = (b0 & 0xF) * 8; v = ((b0 >> 4) & 7) * 8
            w = (b1 & 0xF) * 8; h = (b1 >> 4) * 8
            # crop the drawn rectangle out of the current slot image
            px = bytearray(w*h)
            for yy in range(h):
                sy = v + yy
                if last_img is None or sy >= last_h: continue
                for xx in range(w):
                    sx = u + xx
                    if sx < last_w: px[yy*w + xx] = last_img[sy*last_w + sx]
            pieces.append((footx + x*2, footy + y*2, w, h, bytes(px)))
        return pieces, e & 0x1F, bbox

def render(bank, i, pal_override=None, scale=1):
    """Compose frame i into an RGBA canvas. Returns (w, h, rgba bytes, origin_x, origin_y)."""
    fr = bank.frame(i)
    if not fr: return None
    pieces, pidx, _ = fr
    pal = bank.palette(pal_override if pal_override is not None else pidx) or bank.palette(0)
    xs = [p[0] for p in pieces] + [p[0]+p[2] for p in pieces]
    ys = [p[1] for p in pieces] + [p[1]+p[3] for p in pieces]
    x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
    W, H = x1-x0, y1-y0
    img = bytearray(W*H*4)
    for px_, py_, w, h, pix in pieces:
        for yy in range(h):
            for xx in range(w):
                c = pix[yy*w+xx]
                col = pal[c] if pal else (c, c, c)
                if col is None: continue
                o = ((py_-y0+yy)*W + (px_-x0+xx))*4
                img[o:o+4] = bytes((col[0], col[1], col[2], 255))
    return W, H, bytes(img), -x0, -y0
