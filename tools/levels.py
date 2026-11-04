#!/usr/bin/env python3
"""Eurocom Hercules MAP_PSX.BIN levels (ported from ENG1.DLL FUN_10020670 header, FUN_100208f0 sections,
FUN_1002df20 block meshes, FUN_1002ed10 vertices, FUN_1001fc90 VRAM upload).
Renders an 'unrolled' orthographic front view (the game projects the same meshes along a curved 3D path)."""
import struct
import numpy as np

class Level:
    def __init__(self, data, cif=None):
        d = self.d = data
        self.cif = cif
        self.magic = d[:4]
        u32 = lambda a: struct.unpack_from('<I', d, a)[0]
        self.rel = lambda a: a + (u32(a) & ~3)
        self.counts = struct.unpack_from('<6H', d, 8)
        self.d30 = self.rel(0x28)                     # texture-coordinate records (3 x u32)
        self.d34 = 0x44                               # block table
        # VRAM: 768x512 @ (0,0) and 256x256 @ (768,256), 16-bit words
        self.vram = np.zeros((512, 1024), dtype=np.uint16)
        o1 = self.rel(0x14); o2 = self.rel(0x18)
        def blit(x, y, w, h, off):
            # the game only keeps the resident block, so an image may run past the end: pad with zeros
            n = max(0, min(w*h, (len(d) - off) // 2))
            buf = np.zeros(w*h, dtype=np.uint16); buf[:n] = np.frombuffer(d, dtype='<u2', count=n, offset=off)
            self.vram[y:y+h, x:x+w] = buf.reshape(h, w)
        blit(0, 0, 768, 512, o1); blit(768, 256, 256, 256, o2)
        self.vram8 = self.vram.view(np.uint8).reshape(512, 2048)
        lp = self.rel(0x30)                            # layer 0
        self.nstrips = d[lp]; self.nsections = d[lp+2]
        self.ncols = struct.unpack_from('<H', d, lp+4)[0]; self.H = struct.unpack_from('<H', d, lp+6)[0]
        zs = [u32(lp+12+4*i) for i in range(self.nstrips)]
        self.strip_z = list(np.cumsum(zs))
        self.sec_table = lp + 12 + self.nstrips*4

    def s16(self, a): return struct.unpack_from('<h', self.d, a)[0]

    def section(self, i):
        d = self.d
        sec, nsect = struct.unpack_from('<Hh', d, self.sec_table + 12*i)
        b = sec * 2048
        start = struct.unpack_from('<h', d, b)[0]
        widths = [struct.unpack_from('<h', d, b+2+2*j)[0] for j in range(self.nstrips)]
        off = b + ((self.nstrips + 2) & ~1) * 2
        grids = []
        for w in widths:
            grids.append(np.frombuffer(d, dtype='<u4', count=w*self.H, offset=off).reshape(w, self.H)); off += w*self.H*4
        return start, widths, grids

    def block(self, b):
        d = self.d
        i = self.s16(self.d34 - 4 + b*4) + b
        a = self.d34 - 4 + i*4
        nv, nf = d[a+2], d[a+3]
        verts = []
        for k in range(nv):
            v = struct.unpack_from('<I', d, a+4+4*k)[0]
            x = (v & 0xFF) - 256 if v & 0x80 else v & 0xFF
            y = ((v >> 8) & 0xFFFF); y = y - 65536 if y & 0x8000 else y
            z = (v >> 24) - 256 if v >> 31 else v >> 24
            verts.append((x, y, z))
        faces = []
        fa = a + 8 + nv*4
        for k in range(nf):
            f0, f1 = struct.unpack_from('<II', d, fa + 8*k)
            idx = (f0 & 0x3F, (f0 >> 6) & 0x3F, (f0 >> 12) & 0x3F, (f0 >> 18) & 0x3F)
            faces.append((idx, f0 >> 24, f1 & 0xFFFF))
        return verts, faces

    def texrec(self, off):
        e0, e1, e2 = struct.unpack_from('<III', self.d, self.d30 + off)
        uv = [(e0 & 0xFF, (e0 >> 8) & 0xFF), (e1 & 0xFF, (e1 >> 8) & 0xFF),
              (e2 & 0xFF, (e2 >> 8) & 0xFF), ((e2 >> 16) & 0xFF, (e2 >> 24) & 0xFF)]
        tpage = e1 >> 16
        return uv, tpage

    def clut_rgba(self, row=480, x=768, shade=0):
        if self.cif is not None:
            # PC port: texels are indices into the level's 256-colour hardware palette (MAP_PSX.CIF,
            # loaded by FUN_004068c0); shade levels 1..31 remap through tables at the end of the file.
            pal = np.frombuffer(self.cif, dtype=np.uint8, count=768).reshape(256, 3)
            idx = np.arange(256)
            if shade:
                tab = np.frombuffer(self.cif, dtype=np.uint8, count=256, offset=768+0x40000+0x10000+(shade-1)*256)
                idx = tab
            rgba = np.concatenate([pal[idx], np.full((256, 1), 255, np.uint8)], axis=1)
            rgba[0, 3] = 0
            return rgba
        c = self.vram[row, x:x+256].astype(np.uint32)
        r = (c & 31) * 255 // 31; g = ((c >> 5) & 31) * 255 // 31; b = ((c >> 10) & 31) * 255 // 31
        a = np.where(c == 0, 0, 255)
        return np.stack([r, g, b, a], axis=1).astype(np.uint8)

    def triangles(self, sections=None):
        """Yield (pts[(X,Y)]x3, uvs x3, tpage, depth) for every face of the chosen sections."""
        cache = {}
        secs = range(self.nsections) if sections is None else sections
        seen = set()
        for si in secs:
            start, widths, grids = self.section(si)
            for s, g in enumerate(grids):
                zbase = self.strip_z[s]
                for c in range(widths[s]):
                    col = start + c
                    if (s, col) in seen: continue          # sections overlap
                    seen.add((s, col))
                    for r in range(self.H):
                        cell = int(g[c, r])
                        b = (cell >> 1) & 0x3FF
                        if not b: continue
                        if b not in cache: cache[b] = self.block(b)
                        verts, faces = cache[b]
                        flip = cell & 1
                        P = [(col*64 + (vx if flip else 64 - vx), r*64 + vy, zbase + vz) for vx, vy, vz in verts]
                        for idx, fl, to in faces:
                            if fl & 0x80: continue          # untextured/flat faces skipped in preview
                            uv, tp = self.texrec(to)
                            if any(i >= len(P) for i in idx): continue
                            tris = [(0, 1, 2), (1, 3, 2)] if fl & 0x40 else [(0, 1, 2)]
                            for t in tris:
                                pts = [P[idx[i]] for i in t]
                                yield [(p[0], p[1]) for p in pts], [uv[i] for i in t], tp, sum(p[2] for p in pts)/3, fl

def raster(level, tris, x0, y0, W, H, far_first=True):
    img = np.zeros((H, W, 4), dtype=np.uint8)
    lut = level.clut_rgba()
    tris = sorted(tris, key=lambda t: -t[3] if far_first else t[3])
    v8 = level.vram8
    for pts, uvs, tp, _, fl in tris:
        tx = (tp & 0xF) * 64 * 2; ty = ((tp >> 4) & 1) * 256
        (ax, ay), (bx, by), (cx, cy) = [(p[0]-x0, p[1]-y0) for p in pts]
        den = (by - cy)*(ax - cx) + (cx - bx)*(ay - cy)
        if den == 0: continue
        minx, maxx = max(int(min(ax, bx, cx)), 0), min(int(max(ax, bx, cx))+1, W)
        miny, maxy = max(int(min(ay, by, cy)), 0), min(int(max(ay, by, cy))+1, H)
        if minx >= maxx or miny >= maxy: continue
        X, Y = np.meshgrid(np.arange(minx, maxx) + 0.5, np.arange(miny, maxy) + 0.5)
        l1 = ((by - cy)*(X - cx) + (cx - bx)*(Y - cy)) / den
        l2 = ((cy - ay)*(X - cx) + (ax - cx)*(Y - cy)) / den
        l3 = 1 - l1 - l2
        m = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
        if not m.any(): continue
        u = l1*uvs[0][0] + l2*uvs[1][0] + l3*uvs[2][0]
        v = l1*uvs[0][1] + l2*uvs[1][1] + l3*uvs[2][1]
        ui = np.clip(u.astype(int), 0, 255); vi = np.clip(v.astype(int), 0, 255)
        idx = v8[np.clip(ty + vi, 0, 511), np.clip(tx + ui, 0, 2047)]
        col = lut[idx]
        m &= col[..., 3] > 0
        sub = img[miny:maxy, minx:maxx]
        if fl & 0x20:                                   # PSX semi-transparency, mode = tpage bits 5-6
            mode = (tp >> 5) & 3
            B = sub[..., :3].astype(np.int16); F = col[..., :3].astype(np.int16)
            out = [(B + F) // 2, B + F, B - F, B + F // 4][mode]
            blended = np.concatenate([np.clip(out, 0, 255).astype(np.uint8),
                                      np.full(out.shape[:2] + (1,), 255, np.uint8)], axis=2)
            sub[m] = blended[m]
        else:
            sub[m] = col[m]
    return img
