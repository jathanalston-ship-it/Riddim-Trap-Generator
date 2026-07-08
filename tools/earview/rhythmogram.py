#!/usr/bin/env python3
# rhythmogram — a "modulation rhythmogram": WHERE the growl gnarl / rhythmic
# chug lives across the track. For each time frame we measure how strong the
# bass amplitude-modulation is at musically-relevant modulation RATES tied to
# the beat grid (1/4,1/8,1/16,1/32,1/8-triplet), NOT raw Hz. Reveals the drop's
# rhythmic density and the growl's wobble/chug rate over time — e.g. a drop that
# switches from an 1/8 chug to a 1/16 gallop shows as the hot row moving up.
#   rhythmogram <wav> <out.png> [bpm]     default bpm 145
# Deps: numpy, scipy. No ML.
import sys, os, struct, zlib, numpy as np
from scipy.signal import butter, sosfiltfilt, medfilt, stft, istft

# ---- io (copied from dissect.py so this file is standalone) ----
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

# ---- colormap + PNG (copied from earview/dissect) ----
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

# ---- HPSS (copied from dissect.py): take the HARMONIC/tonal part so the drums'
#      broadband transients don't pollute the growl envelope. In practice the
#      harmonic-then-bandpass envelope tracks the wobble much cleaner than a raw
#      bandpass (kick/snare bleed would add spurious 1/4 & 1/8 energy). ----
def hpss_harm(x,sr,kt=17,kf=17):
    f,t,Z=stft(x,sr,nperseg=2048,noverlap=1536); M=np.abs(Z)
    H=medfilt(M,(1,kt)); P=medfilt(M,(kf,1))
    Hm=(H**2)/(H**2+P**2+1e-9)
    _,xh=istft(Z*Hm,sr,nperseg=2048,noverlap=1536); return xh

RATES=[('1/4',1),('1/8',2),('1/16',4),('1/32',8),('1/8T',3)]  # multiplier on bpm/60
RH=34                 # pixel height of each rate band in the heatmap
ENV_SR=1500.0         # envelope decimation rate (covers up to ~64Hz modulation)

def rhythmogram(x,sr,bpm):
    xh=hpss_harm(x[:min(len(x),int(sr*240))],sr)            # cap at 4min for speed
    b=sosfiltfilt(butter(4,[40/(sr/2),320/(sr/2)],'bp',output='sos'),xh)  # bass/growl band
    # amplitude envelope: full-wave rectify + ~2ms smooth (as in ears.roughness)
    w=max(1,int(0.002*sr)); env=np.convolve(np.abs(b),np.ones(w)/w,'same')
    ds=max(1,int(round(sr/ENV_SR))); env=env[::ds]; esr=sr/ds
    beat=60.0/bpm
    hop=max(1,int(0.125*beat*esr))                          # ~1/8-beat hop
    N=int(2.0*beat*esr)                                     # 2-beat window resolves 1/4-note rate
    if N>=len(env): N=len(env)//2
    fr_freqs=np.array([bpm/60.0*m for _,m in RATES])        # modulation rate in Hz per row
    n=np.arange(N); han=np.hanning(N)
    B=np.exp(-2j*np.pi*np.outer(fr_freqs,n)/esr)*han        # single-bin DFT basis (Goertzel-style)
    starts=range(0,len(env)-N,hop)
    S=[]
    for s in starts:
        e=env[s:s+N]; e=e-e.mean()
        carrier=np.sqrt(np.mean(env[s:s+N]**2))+1e-9        # normalize by frame level -> mod DEPTH
        S.append(np.abs(B@e)/(np.sum(han)*carrier))
    S=np.array(S).T if S else np.zeros((5,1))               # shape (5 rates, F frames)
    # Compensate the natural ~1/f (pink) tilt of amplitude envelopes: without this
    # the slowest rate (1/4) always wins by construction. Weighting each row by its
    # modulation frequency whitens that tilt so the TRUE chug rate stands out and
    # drop switches (1/8 -> 1/16 gallop) become visible.
    S=S*(fr_freqs/fr_freqs.mean())[:,None]
    times=np.array([s/esr for s in starts]) if S.shape[1] else np.zeros(1)
    return S,times,fr_freqs

def render(S,W=1100):
    # global normalize by percentile (like specrow) so faint frames still show
    lo,hi=np.percentile(S,5),np.percentile(S,99.5); Sn=np.clip((S-lo)/(hi-lo+1e-9),0,1)
    xs=(np.arange(W)*(Sn.shape[1]/W)).astype(int)
    sep=np.full((2,W,3),np.array([40,40,55],np.uint8))
    # display order top->bottom must end with 1/4 at the BOTTOM:
    order=[4,3,2,1,0]   # rows: 1/8T, 1/32, 1/16, 1/8, 1/4
    strips=[]
    for ri in order:
        row=hot(Sn[ri][xs])                                # (W,3)
        strips.append(np.repeat(row[None,:,:],RH,0)); strips.append(sep)
    return np.vstack(strips[:-1])

def main():
    if len(sys.argv)<3: print("usage: rhythmogram.py <wav> <out.png> [bpm]"); return
    wav,out=sys.argv[1],sys.argv[2]; bpm=float(sys.argv[3]) if len(sys.argv)>3 else 145.0
    x,sr=load(wav); S,times,ff=rhythmogram(x,sr,bpm)
    write_png(out,render(S))
    labels=[r[0] for r in RATES]
    print(f"# rhythmogram {os.path.basename(wav)} @ {bpm} BPM -> {out}")
    print(f"# rows bottom->top: 1/4  1/8  1/16  1/32  1/8T   (mod rates Hz: "
          +" ".join(f"{l}={f:.1f}" for l,f in zip(labels,ff))+")")
    print("# color=amplitude-modulation depth of the 40-320Hz growl at each beat-relative rate over time")
    tot=S.sum(1); dom=labels[int(np.argmax(tot))]
    print(f"  dominant modulation rate overall: {dom}  "
          +"  ".join(f"{l}:{v/ (tot.sum()+1e-9)*100:4.1f}%" for l,v in zip(labels,tot)))
    # per-quarter-of-track dominant rate (to see the drop switch chug->gallop)
    F=S.shape[1]
    for q in range(4):
        seg=S[:,q*F//4:(q+1)*F//4]
        if seg.shape[1]==0: continue
        e=seg.sum(1); d=labels[int(np.argmax(e))]
        t0=times[q*F//4] if F else 0; t1=times[min(F-1,(q+1)*F//4-1)] if F else 0
        print(f"  Q{q+1} [{t0:5.1f}-{t1:5.1f}s]  dominant: {d}")
main()
