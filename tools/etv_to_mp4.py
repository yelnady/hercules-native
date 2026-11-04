#!/usr/bin/env python3
"""Convert a Hercules .ETV video to an H.264/AAC .mp4 (needs ffmpeg). Usage: etv_to_mp4.py in.etv out.mp4"""
import sys, os, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etv

def main(src, dst):
    e = etv.ETV(src)
    wav = tempfile.mktemp(suffix='.wav'); etv.write_wav(wav, e)
    cmd = ['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '%dx%d' % (e.w, e.h),
           '-r', str(e.fps), '-i', '-', '-i', wav, '-c:v', 'libx264', '-crf', '16', '-preset', 'medium',
           '-pix_fmt', 'yuv420p', '-vf', 'scale=iw*2:ih*2:flags=neighbor', '-c:a', 'aac', '-b:a', '128k',
           '-shortest', '-movflags', '+faststart', dst]
    ff = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    last = None; tabs = None
    for i, w, h, pal, px in e.frames():
        if pal is not last:
            last = pal
            tabs = [bytes(c[k] for c in pal) for k in range(3)]
        buf = bytearray(w*h*3)
        for k in range(3): buf[k::3] = px.translate(tabs[k])
        ff.stdin.write(buf)
    ff.stdin.close(); rc = ff.wait(); os.unlink(wav)
    if etv.REPAIRS: print(os.path.basename(src), 'repairs:', '; '.join(etv.REPAIRS))
    sys.exit(rc)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
