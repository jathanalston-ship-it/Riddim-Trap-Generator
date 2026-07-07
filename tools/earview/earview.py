#!/usr/bin/env python3
# earview — "ears via eyes" for the sound-design loop. Renders audio into images
# a vision model can read, and prints numeric fingerprints. Subcommands:
#   spec   <wav> <bpm> <out.png> [start|auto] [bars] [scorefile] [lane]
#   stems  <dir> <bpm> <out.png> [bars]        # per-lane spectrogram strips + clean onset grids
#   compare<wavA> <wavB> <bpm> <out.png> [bars]# A vs B spectrograms + sub-envelope "pumping" plot
#   fp     <wav> [bars] [bpm]                  # QUANTITATIVE fingerprint: band shares, pump
#                                              # depth, crest, centroid, growl odd/wobble.
#                                              # Auto-detects tempo + drop window if bpm omitted.
# Deps: numpy, scipy (+ stdlib zlib/struct). No matplotlib/PIL.
import sys, os, struct, zlib, glob, numpy as np
from scipy.signal import stft, butter, sosfiltfilt, find_peaks

# ------------------------------------------------------------------ io / dsp
def load_wav(path):
    raw=open(path,'rb').read(); assert raw[:4]==b'RIFF' and raw[8:12]==b'WAVE'
    i=12; sr=44100; ch=2; data=None
    while i+8<=len(raw):
        cid=raw[i:i+4]; csz=struct.unpack('<I',raw[i+4:i+8])[0]; body=i+8
        if cid==b'fmt ': ch=struct.unpack('<H',raw[body+2:body+4])[0]; sr=struct.unpack('<I',raw[body+4:body+8])[0]
        if cid==b'data':
            avail=len(raw)-body; real=avail if (csz==0 or csz>avail) else csz
            data=raw[body:body+real]; break
        i=body+csz+(csz&1)
    x=np.frombuffer(data,dtype='<i2').astype(np.float64)/32768.0
    x=x[:(len(x)//ch)*ch].reshape(-1,ch); return 0.5*(x[:,0]+x[:,1]), sr

def band(x,sr,lo,hi):
    sos=[]
    if lo>20: sos.append(butter(4,lo/(sr/2),'hp',output='sos'))
    if hi<sr/2*0.98: sos.append(butter(4,hi/(sr/2),'lp',output='sos'))
    y=x
    for s in sos: y=sosfiltfilt(s,y)
    return y

def hot(v):
    v=np.clip(v,0,1); r=np.clip(1.7*v,0,1); g=np.clip(1.7*v-0.7,0,1)
    b=np.clip(2.2*v-1.2,0,1)+0.35*np.clip(1-abs(v-0.25)*4,0,1)
    return np.clip(np.stack([r,g,np.clip(b,0,1)],-1)*255,0,255).astype(np.uint8)

def write_png_rgb(path,rgb):
    H,W,_=rgb.shape; raw=bytearray()
    for y in range(H): raw.append(0); raw.extend(rgb[y].tobytes())
    def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))
                          +chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b''))

def spec_img(seg,sr,W,H,fmin=30,fmax=16000):
    f,t,Z=stft(seg,sr,nperseg=2048,noverlap=1536)
    mag=20*np.log10(np.abs(Z)+1e-6)
    logf=np.logspace(np.log10(fmin),np.log10(fmax),H)
    Zl=np.array([mag[np.argmin(np.abs(f-ff))] for ff in logf])
    lo,hi=np.percentile(Zl,5),np.percentile(Zl,99.5)
    img=hot(np.clip((Zl-lo)/(hi-lo+1e-9),0,1))[::-1]
    xs=(np.arange(W)*(img.shape[1]/W)).astype(int)
    return img[:,xs]

def beat_lines(img,bars,col=(80,80,120)):
    W=img.shape[1]
    for b in range(bars*4+1):
        px=int(b*(W/(bars*4)))
        if 0<=px<W: img[:,max(0,px-1):px+1]=np.array(col,np.uint8)
    return img

BANDS={'KICK 40-110':(40,110),'BASS 120-350':(120,350),'SNARE 1.5-5k':(1500,5000),
       'MID 500-2k':(500,2000),'HAT 7-14k':(7000,14000)}
def onset_grid(seg,sr,bpm,bars):
    beat=60.0/bpm; step_s=beat/4; steps=bars*16; out=[]
    for name,(l,h) in BANDS.items():
        bx=band(seg,sr,l,h); w=int(0.006*sr)
        env=np.convolve(np.abs(bx),np.ones(w)/w,'same'); le=np.log(env+1e-4)
        flux=np.diff(le,prepend=le[0]); flux[flux<0]=0
        if flux.max()<=0: out.append((name,'.'*steps)); continue
        pk,_=find_peaks(flux,height=flux.max()*0.22,distance=max(1,int(step_s*sr*0.7)),prominence=flux.max()*0.12)
        on=set(int(round((p/sr)/step_s)) for p in pk)
        out.append((name,''.join('X' if j in on else '.' for j in range(steps))))
    return out

def print_grid(rows,bars,label=''):
    steps=bars*16
    hdr='            '+' '.join(('1234'[(j//4)%4] if j%4==0 else '.') for j in range(steps))
    if label: print(label)
    print(hdr)
    for name,row in rows: print(f"{name:11s} "+' '.join(row))

def pick_window(x,sr,bpm,bars,start):
    winlen=int(bars*(60.0/bpm*4)*sr)
    if start not in (None,'auto'):
        s0=int(float(start)*sr)
    else:
        gb=band(x,sr,120,3000); best=-1; s0=0
        for s in range(0,max(1,len(x)-winlen),int(0.5*sr)):
            e=np.sum(gb[s:s+winlen]**2)
            if e>best: best=e; s0=s
    return x[s0:s0+winlen], s0/sr

# ------------------------------------------------------------------ subcommands
def cmd_spec(a):
    wav,bpm,out=a[0],float(a[1]),a[2]
    start=a[3] if len(a)>3 else 'auto'; bars=int(a[4]) if len(a)>4 else 4
    x,sr=load_wav(wav); seg,t0=pick_window(x,sr,bpm,bars,start)
    img=beat_lines(np.repeat(spec_img(seg,sr,1100,300),2,0),bars)
    if len(a)>6:  # score overlay: green ticks at note-starts for a lane
        scoref,lane=a[5],a[6]; W=img.shape[1]; beat=60.0/bpm; winbeats=bars*4
        for ln in open(scoref):
            if ln.startswith('#') or not ln.strip(): continue
            p=ln.split()
            if p[0]!=lane: continue
            nb=float(p[1])-(t0/beat)
            if 0<=nb<winbeats:
                px=int(nb/winbeats*W); img[:6,max(0,px-1):px+2]=np.array([80,255,80],np.uint8)
        print(f"# overlaid {lane} note-starts (green ticks, top edge)")
    write_png_rgb(out,img)
    print(f"# spec {os.path.basename(wav)}  window {t0:.1f}s {bars} bars @ {bpm} BPM -> {out}")
    print_grid(onset_grid(seg,sr,bpm,bars),bars)

STEM_ORDER=['Sub','BassA','BassB','BassC','Kick','Snare','HatClosed','HatOpen','Perc','Melody','Pad']
def cmd_stems(a):
    d,bpm,out=a[0],float(a[1]),a[2]; bars=int(a[3]) if len(a)>3 else 2
    files={os.path.basename(f)[5:-4]:f for f in glob.glob(os.path.join(d,'stem_*.wav'))}
    order=[s for s in STEM_ORDER if s in files]+[s for s in files if s not in STEM_ORDER]
    # ONE shared window for all stems (aligned in time) — the loudest drop region
    # of the summed stems, so kick/sub/chug line up column-for-column.
    loaded={n:load_wav(files[n]) for n in order}
    sr=next(iter(loaded.values()))[1]
    L=max(len(v[0]) for v in loaded.values())
    mix=np.zeros(L)
    for v in loaded.values(): mix[:len(v[0])]+=v[0]
    _,t0=pick_window(mix,sr,bpm,bars,'auto'); start=str(t0)
    strips=[]; print(f"# stems in {d} @ {t0:.1f}s, {bars} bars (aligned; top->bottom): "+', '.join(order))
    for name in order:
        x,_=loaded[name]; seg,_=pick_window(x,sr,bpm,bars,start)
        strips.append(beat_lines(spec_img(seg,sr,1100,90),bars))
        strips.append(np.full((3,1100,3),np.array([210,210,210],np.uint8)))
        rows=onset_grid(seg,sr,bpm,bars)
        key={'Kick':'KICK 40-110','Sub':'KICK 40-110','BassA':'BASS 120-350','BassB':'BASS 120-350',
             'BassC':'BASS 120-350','Snare':'SNARE 1.5-5k','HatClosed':'HAT 7-14k','HatOpen':'HAT 7-14k',
             'Perc':'HAT 7-14k'}.get(name)
        r=[x for x in rows if x[0]==key]
        print_grid([(name,r[0][1])] if r else [(name,onset_grid(seg,sr,bpm,bars)[3][1])],bars)
    write_png_rgb(out,np.vstack(strips))
    print(f"# wrote stacked stem spectrograms -> {out}  (each strip 90px; separators light-grey)")

def cmd_compare(a):
    wa,wb,bpm,out=a[0],a[1],float(a[2]),a[3]; bars=int(a[4]) if len(a)>4 else 2
    panels=[]; subenvs=[]
    for wav in (wa,wb):
        x,sr=load_wav(wav); seg,t0=pick_window(x,sr,bpm,bars,'auto')
        panels.append(beat_lines(spec_img(seg,sr,1100,240),bars))
        panels.append(np.full((3,1100,3),np.array([255,255,255],np.uint8)))
        sb=band(seg,sr,30,120); w=int(0.01*sr)
        e=np.convolve(np.abs(sb),np.ones(w)/w,'same'); e/=(e.max()+1e-9); subenvs.append(e)
    EH=120; env=beat_lines(np.zeros((EH,1100,3),np.uint8),bars,(60,60,90))
    for e,c in zip(subenvs,[(255,80,80),(80,255,80)]):
        xs=np.linspace(0,len(e)-1,1100).astype(int); ev=e[xs]
        for px in range(1100):
            y=int((1-ev[px])*(EH-1)); env[max(0,y-1):y+1,px]=np.array(c,np.uint8)
    panels.append(env)
    write_png_rgb(out,np.vstack(panels))
    print(f"# compare: top=A({os.path.basename(wa)})  mid=B({os.path.basename(wb)})  bottom=sub-envelope -> {out}")
    print("# bottom plot: red=A green=B sub(30-120Hz) level. DEEP DIPS = sidechain pumping; a FLAT HIGH line = a wall.")
    for wav,lab in zip((wa,wb),['A generated','B reference']):
        x,sr=load_wav(wav); seg,_=pick_window(x,sr,bpm,bars,'auto')
        print_grid(onset_grid(seg,sr,bpm,bars),bars,label=f"## {lab}: {os.path.basename(wav)}")

# ------------------------------------------------------------------ fingerprint
# Quantitative features so tracks compare on the same axes (not just by eye).
# Reference band shares (drop, 4 bars): riddim sits subLow ~58-65, sub 8-16,
# bass 5-8, mid(800-2500) 5-14, hi+air 4-14; deep-pump refs dipRatio 0.9,
# wall-style refs ~0.3; growl oddRatio 0.5-0.63 (square-ish).
def _tempo(x,sr):
    w=int(0.01*sr); e=np.convolve(np.abs(x),np.ones(w)/w,'same')
    le=np.log(e+1e-4); flux=np.diff(le,prepend=le[0]); flux[flux<0]=0
    ds=max(1,int(sr/200)); f=flux[::ds]; f=f-f.mean()
    ac=np.correlate(f,f,'full')[len(f)-1:]; best=(120.0,0)
    for bpm in np.arange(70,190,0.25):
        lag=int((60.0/bpm)*200)
        if lag<len(ac) and ac[lag]>best[1]: best=(bpm,ac[lag])
    return best[0]

def _bandshares(seg,sr):
    f,t,Z=stft(seg,sr,nperseg=4096,noverlap=3072); P=np.abs(Z)**2
    edges=[(20,60),(60,120),(120,300),(300,800),(800,2500),(2500,6000),(6000,16000)]
    names=['subLow','sub','bass','lowmid','mid','hi','air']; tot=P.sum()+1e-12
    return {nm:float(P[(f>=lo)&(f<hi)].sum()/tot) for (lo,hi),nm in zip(edges,names)}

def _pump(seg,sr):
    sb=band(seg,sr,30,120); w=int(0.01*sr)
    e=np.convolve(np.abs(sb),np.ones(w)/w,'same'); e/=(e.max()+1e-9)
    lo=np.percentile(e,15); hi=np.percentile(e,85)
    return dict(dip=1.0-lo/(hi+1e-9),p15=float(lo),p85=float(hi))

def _growl(seg,sr):
    b=band(seg,sr,120,1200); c=b[len(b)//3:len(b)//3+int(0.3*sr)]
    ac=np.correlate(c,c,'full')[len(c)-1:]; lo=int(sr/400); hi=int(sr/40)
    f0=sr/(lo+np.argmax(ac[lo:hi])) if hi<len(ac) else 80.0
    N=len(b); freqs=np.fft.rfftfreq(N,1/sr); mag=np.abs(np.fft.rfft(b))
    at=lambda fr:(lambda k:mag[max(0,k-2):k+3].sum())(int(np.argmin(np.abs(freqs-fr))))
    odd=sum(at(f0*h) for h in (1,3,5,7,9)); even=sum(at(f0*h) for h in (2,4,6,8))
    env=np.abs(band(seg,sr,120,4000)); w=int(0.004*sr)
    env=np.convolve(env,np.ones(w)/w,'same')[::max(1,int(sr/400))]; env=env-env.mean()
    sp=np.abs(np.fft.rfft(env*np.hanning(len(env)))); ef=np.fft.rfftfreq(len(env),1/400.0)
    m=(ef>=2)&(ef<=16); wob=float(ef[m][np.argmax(sp[m])]) if m.any() else 0.0
    return dict(f0=float(f0),odd=odd/(odd+even+1e-9),wob=wob)

def cmd_fp(a):
    wav=a[0]; bars=int(a[1]) if len(a)>1 else 4
    x,sr=load_wav(wav); bpm=float(a[2]) if len(a)>2 else round(_tempo(x,sr),1)
    seg,t0=pick_window(x,sr,bpm,bars,'auto')
    bs=_bandshares(seg,sr); pd=_pump(seg,sr); gr=_growl(seg,sr)
    rms=np.sqrt(np.mean(seg**2))+1e-9; pk=np.max(np.abs(seg))+1e-9
    f,_,Z=stft(seg,sr,nperseg=2048,noverlap=1536); Pm=np.abs(Z).mean(1)
    cen=(f*Pm).sum()/(Pm.sum()+1e-9)
    print(f"=== {os.path.basename(wav)}  bpm~{bpm:.1f}  drop@{t0:.1f}s  {bars}bars  "
          f"crest {20*np.log10(pk/rms):.1f}dB  centroid {cen:.0f}Hz")
    print("  bands%: "+"  ".join(f"{k} {v*100:4.1f}" for k,v in bs.items()))
    print(f"  PUMP dipRatio {pd['dip']:.2f} (0=wall 1=to-silence)  p15 {pd['p15']:.2f} p85 {pd['p85']:.2f}")
    print(f"  GROWL f0 {gr['f0']:.0f}Hz  oddRatio {gr['odd']:.2f}  wobble {gr['wob']:.1f}Hz")

def main():
    if len(sys.argv)<2: print("usage: earview.py spec|stems|compare|fp ..."); return
    c=sys.argv[1]
    if c=='stems': cmd_stems(sys.argv[2:])
    elif c=='compare': cmd_compare(sys.argv[2:])
    elif c=='fp': cmd_fp(sys.argv[2:])
    elif c=='spec': cmd_spec(sys.argv[2:])
    else: cmd_spec(sys.argv[1:])   # back-compat: earview.py <wav> <bpm> <out> ...
main()
