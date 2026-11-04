import os, sys, struct
def load(path):
    d=open(path,'rb').read()
    h=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,h+6)[0]; opt=struct.unpack_from('<H',d,h+20)[0]
    base=struct.unpack_from('<I',d,h+52)[0]; secs=[]
    for i in range(n):
        s=h+24+opt+40*i; vs,va,rs,raw=struct.unpack_from('<IIII',d,s+8); secs.append((va,vs,raw,rs))
    def rd(va,n):
        r=va-base
        for (sva,vs,raw,rs) in secs:
            if sva<=r<sva+max(vs,rs):
                o=raw+r-sva
                if r-sva+n<=rs: return d[o:o+n]
                return bytes(n)
        return None
    return rd
rd=load(sys.argv[1])
u16=lambda a: struct.unpack('<H',rd(a,2))[0]
u32=lambda a: struct.unpack('<I',rd(a,4))[0]
s16=lambda a: struct.unpack('<h',rd(a,2))[0]
a=0x1003b900
while True:
    lv=u16(a)
    if lv==999: break
    print('level',lv,'entry',hex(a))
    names=['+0','+2','+4','+6','+8','+a','+c','+e','+10','+12','+14','+16']
    print('  shorts', [hex(u16(a+i)) for i in range(0,0x18,2)])
    for off in range(0x18,0x48,4): print('  +%x'%off, hex(u32(a+off)))
    a+=0x48

def spawns(fs_path, mapname, level_entry):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from herc_fs import FS
    fs=FS(fs_path)
    d=fs.get('O:\\%s\\MAP_PSX.BIN'%mapname)
    U16=lambda a: struct.unpack_from('<H',d,a)[0]; S16=lambda a: struct.unpack_from('<h',d,a)[0]
    U32=lambda a: struct.unpack_from('<I',d,a)[0]
    rel=lambda a: a+(U32(a)&~3)
    npaths=U16(0xc); P0=rel(0x30)
    tmpl=u32(level_entry+0x40)
    out=[]
    for i in range(npaths):
        e=P0+16*i; n=U16(e+0xc); so=e+0xe+S16(e+0xe)
        for k in range(n):
            dep,y,lat,t=struct.unpack_from('<HHhH',d,so+8*k)
            ta=tmpl+t*10
            dx,dy,dz,fl,ty=s16(ta),s16(ta+2),s16(ta+4),u16(ta+6),u16(ta+8)
            out.append((i,dep+dx,y+dy,lat+dz,fl,ty,t))
    return out
if len(sys.argv)>2:
    lvl={'GAUNTLET':0x1003b900,'CYCLOPS':0x1003b948,'TORMENT':0x1003b990}
    import collections
    for m in sys.argv[3:]:
        sp=spawns(sys.argv[2],m,lvl[m]); c=collections.Counter(s[5] for s in sp)
        print(m,len(sp),'spawns; types:',', '.join('%x:%d'%(k,v) for k,v in sorted(c.items())))
        for s in sp[:400]: print('   path %d depth %d y %d lat %d flags %x type %x tmpl %d'%s)
if len(sys.argv)==2 or (len(sys.argv)>2 and sys.argv[2]=='types'):
    pass
