#!/usr/bin/env python3
# dissect — new "eyes": pull a track (incl. a REFERENCE we don't have stems for)
# apart into its parts and read them.
#   hpss  <wav> <out.png>         harmonic/percussive separation (tonal bass/melody
#                                 vs drums) via median filtering — isolate a
#                                 reference's growl/sub from its drums.
#   bass  <wav> [bpm]             transcribe the bassline: autocorrelation pitch
#                                 track on the harmonic low end -> note/MIDI list.
# Deps: numpy, scipy. No ML (source-separation models are network-blocked here).
import sys, os, struct, zlib, numpy as np
from scipy.signal import stft, istft, medfilt, butter, sosfiltfilt

def load(path):
    raw=open(path,'rb').read(); i=12; sr=48000; ch=2; data=None
    while i+8<=len(raw):
        cid=raw[i:i+4]; csz=struct.unpack('<I',raw[i+4:i+8])[0]; body=i+8
        if cid==b'fmt ': ch=struct.unpack('<H',raw[body+2:body+4])[0]; sr=struct.unpack('<I',raw[body+4:body+8])[0]
        if cid==b'data':
            avail=len(raw)-body; real=avail if (csz==0 or csz>avail) else csz
            data=raw[body:body+real]; break
        i=body+csz+(csz&1)
    x=np.frombuffer(data,dtype='<i2').astype(np.float64)/32768.0
    x=x[:(len(x)//ch)*ch].reshape(-1,ch); return 0.5*(x[:,0]+x[:,1]), sr

# ---- HPSS: harmonic (sustained -> smooth across TIME) vs percussive (transient
#      -> smooth across FREQUENCY). Soft Wiener masks on the magnitude. ----
def hpss(x,sr,kt=17,kf=17):
    f,t,Z=stft(x,sr,nperseg=2048,noverlap=1536)
    M=np.abs(Z)
    H=medfilt(M,(1,kt))          # median over time -> horizontal/tonal
    P=medfilt(M,(kf,1))          # median over freq -> vertical/percussive
    Hm=(H**2)/(H**2+P**2+1e-9); Pm=1-Hm
    _,xh=istft(Z*Hm,sr,nperseg=2048,noverlap=1536)
    _,xp=istft(Z*Pm,sr,nperseg=2048,noverlap=1536)
    return xh,xp

# ---- small PNG (reuse earview's writer style) ----
def hot(v):
    v=np.clip(v,0,1); r=np.clip(1.7*v,0,1); g=np.clip(1.7*v-0.7,0,1)
    b=np.clip(2.2*v-1.2,0,1)+0.35*np.clip(1-abs(v-0.25)*4,0,1)
    return np.clip(np.stack([r,g,np.clip(b,0,1)],-1)*255,0,255).astype(np.uint8)
def write_png(path,rgb):
    H,W,_=rgb.shape; raw=bytearray()
    for y in range(H): raw.append(0); raw.extend(rgb[y].tobytes())
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))
                          +ch(b'IDAT',zlib.compress(bytes(raw),9))+ch(b'IEND',b''))
def specrow(x,sr,W,H,fmin=30,fmax=12000):
    f,t,Z=stft(x,sr,nperseg=2048,noverlap=1536); mag=20*np.log10(np.abs(Z)+1e-6)
    lf=np.logspace(np.log10(fmin),np.log10(fmax),H)
    Zl=np.array([mag[np.argmin(np.abs(f-ff))] for ff in lf])
    lo,hi=np.percentile(Zl,5),np.percentile(Zl,99.5)
    img=hot(np.clip((Zl-lo)/(hi-lo+1e-9),0,1))[::-1]
    xs=(np.arange(W)*(img.shape[1]/W)).astype(int); return img[:,xs]

def cmd_hpss(a):
    wav,out=a[0],a[1]; x,sr=load(wav)
    seg=x[:min(len(x),int(12*sr))]              # first 12s is plenty to see
    xh,xp=hpss(seg,sr)
    sep=np.full((3,1100,3),np.array([210,210,210],np.uint8))
    img=np.vstack([specrow(seg,sr,1100,150),sep,specrow(xh,sr,1100,150),sep,specrow(xp,sr,1100,150)])
    write_png(out,img)
    eh=np.sqrt(np.mean(xh**2)); ep=np.sqrt(np.mean(xp**2))
    print(f"# {os.path.basename(wav)} -> {out}  (top=full, mid=HARMONIC tonal, bottom=PERCUSSIVE)")
    print(f"# tonal/percussive energy ratio: {20*np.log10(eh/(ep+1e-9)):+.1f} dB")

CH=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def hz2note(f):
    if f<=0: return '--',0
    m=int(round(12*np.log2(f/440.0)+69)); return CH[m%12]+str(m//12-1), m
def cmd_bass(a):
    wav=a[0]; bpm=float(a[1]) if len(a)>1 else 145.0; x,sr=load(wav)
    seg=x[:min(len(x),int(16*sr))]
    xh,_=hpss(seg,sr)                            # isolate the tonal part first
    b=sosfiltfilt(butter(4,[35/(sr/2),350/(sr/2)],'bp',output='sos'),xh)  # bass register
    beat=60/bpm; hop=int(beat/4*sr); win=int(0.09*sr); notes=[]
    for s in range(0,len(b)-win,hop):
        w=b[s:s+win]*np.hanning(win); ac=np.correlate(w,w,'full')[len(w)-1:]
        lo,hi=int(sr/350),int(sr/35)
        if hi>=len(ac) or np.sqrt(np.mean(w**2))<0.003: notes.append((s/sr,0)); continue
        pk=lo+np.argmax(ac[lo:hi]); notes.append((s/sr,sr/pk if pk else 0))
    # segment into note events (merge consecutive same-note frames)
    print(f"# bassline of {os.path.basename(wav)} @ {bpm} BPM (tonal/HPSS + bass-bandpass pitch track)")
    cur=None; t0=0
    for tt,fr in notes+[(notes[-1][0]+1,0)]:
        nm,_=hz2note(fr) if fr>0 else ('--',0)
        base=nm[:-1] if nm!='--' else '--'       # ignore octave jumps for the line
        if base!=cur:
            if cur and cur!='--': print(f"  {t0:5.2f}s  {cur}  ({(tt-t0):.2f}s)")
            cur=base; t0=tt
    # dominant pitch class
    from collections import Counter
    pcs=[hz2note(fr)[0][:-1] for _,fr in notes if fr>0]
    if pcs: print("  root/dominant pitch classes: "+", ".join(f"{n}:{c}" for n,c in Counter(pcs).most_common(4)))

def main():
    if len(sys.argv)<3: print("usage: dissect.py hpss <wav> <out.png> | bass <wav> [bpm]"); return
    c=sys.argv[1]
    if c=='hpss': cmd_hpss(sys.argv[2:])
    elif c=='bass': cmd_bass(sys.argv[2:])
main()
