#!/usr/bin/env python3
# "Ears via eyes": render a WAV as (1) a log-frequency spectrogram PNG I can look
# at, and (2) a beat-aligned per-band onset grid I can read as text. Deps: numpy,
# scipy, zlib/struct (stdlib). Usage: earview.py <wav> <bpm> <out_png> [start_s] [bars]
import sys, struct, zlib, numpy as np
from scipy.signal import stft, butter, sosfiltfilt

def load_wav(path):
    raw = open(path,'rb').read()
    assert raw[:4]==b'RIFF' and raw[8:12]==b'WAVE'
    i=12; sr=44100; ch=2; data=None
    while i+8<=len(raw):
        cid=raw[i:i+4]; csz=struct.unpack('<I',raw[i+4:i+8])[0]; body=i+8
        if cid==b'fmt ':
            ch=struct.unpack('<H',raw[body+2:body+4])[0]; sr=struct.unpack('<I',raw[body+4:body+8])[0]
        if cid==b'data':
            avail=len(raw)-body; real=avail if (csz==0 or csz>avail) else csz
            data=raw[body:body+real]; break
        i=body+csz+(csz&1)
    x=np.frombuffer(data,dtype='<i2').astype(np.float64)/32768.0
    x=x[:(len(x)//ch)*ch].reshape(-1,ch)
    return 0.5*(x[:,0]+x[:,1]), sr

def hot(v):  # v in [0,1] -> (r,g,b) black->purple->orange->white (magma-ish)
    v=np.clip(v,0,1)
    r=np.clip(1.7*v,0,1); g=np.clip(1.7*v-0.7,0,1); b=np.clip(2.2*v-1.2,0,1)+0.35*np.clip(1-abs(v-0.25)*4,0,1)
    return np.clip(np.stack([r,g,np.clip(b,0,1)],-1)*255,0,255).astype(np.uint8)

def write_png_rgb(path, rgb):  # rgb: (H,W,3) uint8
    H,W,_=rgb.shape; raw=bytearray()
    for y in range(H):
        raw.append(0); raw.extend(rgb[y].tobytes())
    def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    ihdr=struct.pack('>IIBBBBB',W,H,8,2,0,0,0)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',ihdr)+chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b''))

def band(x, sr, lo, hi):
    sos=[]
    if lo>20: sos.append(butter(4, lo/(sr/2),'hp',output='sos'))
    if hi<sr/2*0.98: sos.append(butter(4, hi/(sr/2),'lp',output='sos'))
    y=x
    for s in sos: y=sosfiltfilt(s,y)
    return y

def main():
    wav=sys.argv[1]; bpm=float(sys.argv[2]); out=sys.argv[3]
    x,sr=load_wav(wav)
    # pick a window: given start_s or the loudest `bars`-bar window
    beat=60.0/bpm; barsec=beat*4
    bars=int(sys.argv[5]) if len(sys.argv)>5 else 4
    winlen=int(bars*barsec*sr)
    if len(sys.argv)>4 and sys.argv[4]!='auto':
        s0=int(float(sys.argv[4])*sr)
    else:
        gb=band(x,sr,120,3000); best=-1; s0=0
        for s in range(0,max(1,len(x)-winlen),int(0.5*sr)):
            e=np.sum(gb[s:s+winlen]**2)
            if e>best: best=e; s0=s
    seg=x[s0:s0+winlen]
    # --- spectrogram (log-freq) ---
    f,t,Z=stft(seg,sr,nperseg=2048,noverlap=1536)
    mag=20*np.log10(np.abs(Z)+1e-6)
    fmin,fmax=30,16000
    logf=np.logspace(np.log10(fmin),np.log10(fmax),300)
    Zl=np.zeros((len(logf),mag.shape[1]))
    for i,ff in enumerate(logf):
        k=np.argmin(np.abs(f-ff)); Zl[i]=mag[k]
    lo,hi=np.percentile(Zl,5),np.percentile(Zl,99.5)
    norm=np.clip((Zl-lo)/(hi-lo+1e-9),0,1)
    img=hot(norm)[::-1]  # low freq at bottom
    # upscale to a readable size + draw beat gridlines
    H,W=img.shape[:2]; targetW=1100
    xs=(np.arange(targetW)*(W/targetW)).astype(int); img=img[:,xs]
    img=np.repeat(img,2,axis=0)  # taller
    Wt=img.shape[1]
    for b in range(bars*4+1):  # beat lines
        px=int(b*(Wt/(bars*4)))
        if px<Wt: img[:,max(0,px-1):px+1]=np.array([80,80,120],np.uint8)
    write_png_rgb(out,img)
    # --- onset grid (per band, quantized to 16ths over the window) ---
    steps=bars*16; step_s=beat/4
    bands={'KICK 40-110':(40,110),'BASS 120-350':(120,350),'SNARE 1.5-5k':(1500,5000),
           'MID 500-2k':(500,2000),'HAT 7-14k':(7000,14000)}
    print(f"# window {s0/sr:.1f}s, {bars} bars @ {bpm} BPM  (step=1/16 note)")
    header='            '+' '.join(('%-1s'%('1234'[ (j//4)%4 ] if j%4==0 else '.')) for j in range(steps))
    print(header)
    from scipy.signal import find_peaks
    for name,(l,h) in bands.items():
        bx=band(seg,sr,l,h)
        w=int(0.006*sr); env=np.convolve(np.abs(bx),np.ones(w)/w,'same')
        # onset strength = positive derivative of the (log) envelope: fires on
        # NEW hits, not sustained energy (fixes the "every step" over-trigger).
        le=np.log(env+1e-4); flux=np.diff(le,prepend=le[0]); flux[flux<0]=0
        if flux.max()<=0: print(f"{name:11s} "+' '.join('.'*steps)); continue
        # real onsets = prominent local peaks, min ~1/16-note apart (refractory)
        pk,_=find_peaks(flux, height=flux.max()*0.22, distance=int(step_s*sr*0.7),
                        prominence=flux.max()*0.12)
        onsets=set(int(round((p/sr)/step_s)) for p in pk)
        row=['X' if j in onsets else '.' for j in range(steps)]
        print(f"{name:11s} "+' '.join(row))
    print("            (X=onset at that 16th step; columns 1..4 mark the beats)")

main()
