#!/usr/bin/env python3
# ears — PERCEPTUAL audio analysis: turns the spectral "eyes" into "ears" by
# measuring what a listener actually perceives, not raw energy. Descriptors:
#   loudness   K-weighted (BS.1770-ish) integrated loudness (LUFS)
#   roughness  sensory dissonance = amplitude-modulation 15-150 Hz (the "gnarl"
#              of a distorted growl — this IS perceived aggression/dirt)
#   sharpness  Zwicker-weighted high-frequency emphasis (perceived brightness)
#   bark[24]   loudness per critical band (perceptual frequency, not linear Hz)
#   mfcc[13]   timbre envelope shape
# Plus `dist <a> <b>`: a single perceptual distance (how close A SOUNDS to B),
# broken down per dimension. Deps: numpy, scipy (stdlib otherwise).
import sys, os, struct, numpy as np
from scipy.signal import stft, butter, sosfiltfilt, lfilter

# ------------------------------------------------------------------ io
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

def drop_window(x,sr,bpm=145,bars=4):
    m=x; wl=int(bars*4*60/bpm*sr)
    sub=sosfiltfilt(butter(4,110/(sr/2),'lp',output='sos'),m)
    mid=sosfiltfilt(butter(4,[400/(sr/2),3000/(sr/2)],'bp',output='sos'),m)
    best=-1;s0=0
    for s in range(0,max(1,len(m)-wl),int(0.5*sr)):
        e=np.sum(sub[s:s+wl]**2)*np.sum(mid[s:s+wl]**2)
        if e>best:best=e;s0=s
    return x[s0:s0+wl], s0/sr

# ------------------------------------------------------------------ perceptual features
def hz2bark(f): return 13*np.arctan(0.00076*f)+3.5*np.arctan((f/7500.0)**2)

def kweight_loudness(x,sr):
    # BS.1770 K-weighting: a high-shelf (~+4 dB) + a high-pass (~38 Hz), then mean-square.
    # Stage 1 high shelf (approx coeffs for 48k, scaled by sr).
    f0=1681.9; G=3.999; Q=0.7071
    K=np.tan(np.pi*f0/sr); Vh=10**(G/20); Vb=Vh**0.4995
    a0=1+K/Q+K*K
    b=[(Vh+Vb*K/Q+K*K)/a0,2*(K*K-Vh)/a0,(Vh-Vb*K/Q+K*K)/a0]
    a=[1,2*(K*K-1)/a0,(1-K/Q+K*K)/a0]
    y=lfilter(b,a,x)
    # Stage 2 high-pass ~38 Hz
    f0=38.0; Q=0.5; K=np.tan(np.pi*f0/sr); a0=1+K/Q+K*K
    b=[1/a0,-2/a0,1/a0]; a=[1,2*(K*K-1)/a0,(1-K/Q+K*K)/a0]
    y=lfilter(b,a,y)
    ms=np.mean(y**2); return -0.691+10*np.log10(ms+1e-12)

def roughness(x,sr):
    # Sensory dissonance: strength of amplitude modulation 15-150 Hz (peak ~70)
    # summed across critical bands. High for detuned/distorted growls = "gnarl".
    edges=[20,60,120,250,500,1000,2000,4000,8000]
    total=0.0; env_sr=sr
    for lo,hi in zip(edges[:-1],edges[1:]):
        b=sosfiltfilt(butter(4,[lo/(sr/2),hi/(sr/2)],'bp',output='sos'),x)
        env=np.abs(b)                                  # amplitude envelope
        # modulation spectrum of the envelope
        E=np.abs(np.fft.rfft((env-env.mean())*np.hanning(len(env))))
        mf=np.fft.rfftfreq(len(env),1/env_sr)
        # roughness weighting: triangular peak at 70 Hz, 0 at 15 and 300
        w=np.clip(1-np.abs(mf-70)/70,0,1)*((mf>=15)&(mf<=300))
        carrier=np.sqrt(np.mean(b**2))+1e-9
        total+=np.sum(E*w)/ (len(env)*carrier)          # normalize by carrier + length
    return float(total*100)

def sharpness(x,sr):
    # Zwicker sharpness: bark-loudness weighted by g(z) that rises for z>16 Barks.
    f,t,Z=stft(x,sr,nperseg=2048,noverlap=1024); P=(np.abs(Z)**2).mean(1)
    z=hz2bark(f); N=P**0.23                             # specific loudness approx
    g=np.where(z<16,1.0,0.066*np.exp(0.171*z))
    num=np.sum(g*z*N); den=np.sum(N)+1e-12
    return float(num/den)

def bark_spectrum(x,sr):
    f,t,Z=stft(x,sr,nperseg=2048,noverlap=1024); P=(np.abs(Z)**2).mean(1)
    z=hz2bark(f); out=np.zeros(24)
    for k in range(24):
        m=(z>=k)&(z<k+1)
        if m.any(): out[k]=(P[m].sum())**0.23
    return out/(out.sum()+1e-12)

def mfcc(x,sr,n=13):
    f,t,Z=stft(x,sr,nperseg=2048,noverlap=1024); P=(np.abs(Z)**2).mean(1)
    # mel filterbank (26 bands)
    mel=lambda h:2595*np.log10(1+h/700); imel=lambda m:700*(10**(m/2595)-1)
    mp=np.linspace(mel(30),mel(min(16000,sr/2)),28); hz=imel(mp)
    fb=np.zeros((26,len(f)))
    for i in range(26):
        lo,ce,hi=hz[i],hz[i+1],hz[i+2]
        fb[i]=np.clip((f-lo)/(ce-lo),0,1)*(f<=ce)+np.clip((hi-f)/(hi-ce),0,1)*(f>ce)
    e=np.log(fb@P+1e-9)
    from scipy.fft import dct
    c=dct(e, type=2, norm='ortho')   # mel-cepstrum
    return c[:n]

def features(x,sr,bpm=145):
    seg,t0=drop_window(x,sr,bpm)
    return dict(loudness=kweight_loudness(seg,sr), roughness=roughness(seg,sr),
                sharpness=sharpness(seg,sr), bark=bark_spectrum(seg,sr),
                mfcc=mfcc(seg,sr), t0=t0)

def cmd_ears(a):
    path=a[0]; bpm=float(a[1]) if len(a)>1 else 145.0
    x,sr=load(path); F=features(x,sr,bpm)
    print(f"=== {os.path.basename(path)}  drop@{F['t0']:.0f}s ===")
    print(f"  loudness  {F['loudness']:6.1f} LUFS (K-weighted)")
    print(f"  roughness {F['roughness']:6.1f}      (perceived aggression/gnarl — distorted growl = high)")
    print(f"  sharpness {F['sharpness']:6.2f}      (perceived brightness/harshness)")
    bk=F['bark']; lab=['sub','','low','','','lo-mid','','','mid','','','','hi-mid','','','','','pres','','','','air','','']
    print("  bark loudness (24 critical bands, sub->air):")
    print("   "+"".join("█▓▒░ "[min(4,int((1-v/ (bk.max()+1e-9))*5))] for v in bk))

def cmd_dist(a):
    pa,pb=a[0],a[1]; bpm=float(a[2]) if len(a)>2 else 145.0
    xa,sr=load(pa); xb,_=load(pb)
    A=features(xa,sr,bpm); B=features(xb,sr,bpm)
    # per-dimension perceptual deltas (normalized to perceptual just-noticeable scales)
    dl=abs(A['loudness']-B['loudness'])            # LU
    dr=abs(A['roughness']-B['roughness'])/max(1,B['roughness'])*100
    ds=abs(A['sharpness']-B['sharpness'])/max(0.1,B['sharpness'])*100
    dbk=np.sqrt(np.mean((A['bark']-B['bark'])**2))*100
    dmf=np.linalg.norm(A['mfcc']-B['mfcc'])
    print(f"PERCEPTUAL DISTANCE  {os.path.basename(pa)}  vs  {os.path.basename(pb)}")
    print(f"  loudness   Δ {dl:5.1f} LU")
    print(f"  roughness  Δ {dr:5.0f}%   (aggression/gnarl match)")
    print(f"  sharpness  Δ {ds:5.0f}%   (brightness match)")
    print(f"  timbre     Δ {dbk:5.1f} (bark)  {dmf:5.1f} (mfcc)")
    score=100*np.exp(-(dr/120+ds/120+dbk/8+dmf/25))
    print(f"  -> overall perceptual similarity: {score:4.0f}/100")

# ------------------------------------------------------------------ semantic ears
# Map the perceptual feature vector to human-readable descriptors (the "what does
# it sound like" layer). Thresholds calibrated from the riddim reference set
# (Seleman/MSTIC/ETERNAL/Aweminus/BULLETS measured with this tool). A generic
# pretrained tagger (YAMNet/CLAP) is blocked by the network policy here, so this
# reference-grounded vocabulary is the practical "semantic ears".
def _bucket(v, edges, words):
    for e,w in zip(edges,words):
        if v<e: return w
    return words[-1]

def describe(F):
    d=[]
    d.append(_bucket(F['roughness'], [600,1000,1400], ['smooth/clean','moderate','aggressive','extremely distorted']))
    d.append(_bucket(F['sharpness'], [40,52,64], ['dark/warm','balanced','bright','harsh/piercing']))
    bk=F['bark']; air=bk[18:].sum(); mid=bk[8:16].sum()
    if air>0.10: d.append('airy top')
    if mid<0.12: d.append('scooped mids')
    d.append(_bucket(F['loudness'], [-12,-9,-7], ['dynamic','loud','very loud','brickwalled']))
    return d

def cmd_describe(a):
    path=a[0]; bpm=float(a[1]) if len(a)>1 else 145.0
    x,sr=load(path); F=features(x,sr,bpm)
    print(f"=== {os.path.basename(path)} SOUNDS LIKE ===")
    print("  "+", ".join(describe(F)))
    print(f"  [roughness {F['roughness']:.0f}  sharpness {F['sharpness']:.1f}  loudness {F['loudness']:.1f} LUFS]")

def cmd_nearest(a):
    # classify a track against a folder of reference wavs: which does it sound
    # most like? usage: nearest <wav> <refdir> [bpm]
    path=a[0]; refdir=a[1]; bpm=float(a[2]) if len(a)>2 else 145.0
    import glob
    x,sr=load(path); A=features(x,sr,bpm)
    print(f"=== {os.path.basename(path)} — nearest reference ===")
    scores=[]
    for rf in sorted(glob.glob(os.path.join(refdir,'*.wav'))):
        xb,_=load(rf); B=features(xb,sr,bpm)
        dr=abs(A['roughness']-B['roughness'])/max(1,B['roughness'])
        ds=abs(A['sharpness']-B['sharpness'])/max(0.1,B['sharpness'])
        dbk=np.sqrt(np.mean((A['bark']-B['bark'])**2))
        dmf=np.linalg.norm(A['mfcc']-B['mfcc'])
        sim=100*np.exp(-(dr+ds+dbk*12+dmf/25))
        scores.append((sim,os.path.basename(rf)))
    for sim,nm in sorted(scores,reverse=True):
        print(f"  {sim:4.0f}/100  {nm}")

# ------------------------------------------------------------------ knowledge store
# Optional: persist every measurement into the RTG Knowledge Store so it is never
# thrown away and future analyzers can consume it. OFF unless --record is passed;
# without it, ears.py behaves byte-for-byte as before.
KVER = "ears.py/1"

def _record_features(path, F, opts):
    _kdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'knowledge')
    sys.path.insert(0, os.path.normpath(_kdir))
    try:
        from kstore import KnowledgeStore
    except Exception as e:
        print(f"  [record skipped: knowledge store unavailable: {e}]"); return
    kind = opts.get('kind') or 'render'
    with KnowledgeStore(opts.get('db')) as ks:
        name = opts.get('subject') or os.path.basename(path)
        sid = (ks.subject(kind, name) if opts.get('subject')
               else ks.subject_for_file(path, kind))
        ks.record(sid, "loudness.integrated_lufs", float(F['loudness']), unit="LUFS",
                  window="drop", tool="ears.py", tool_version=KVER, method="BS.1770 K-weighted")
        ks.record(sid, "roughness.am_gnarl", float(F['roughness']),
                  window="drop", tool="ears.py", tool_version=KVER, method="15-150Hz AM energy")
        ks.record(sid, "sharpness.zwicker_acum", float(F['sharpness']), unit="acum",
                  window="drop", tool="ears.py", tool_version=KVER, method="Zwicker 24-band")
        ks.record(sid, "bark.spectrum", vector=[float(v) for v in F['bark']],
                  window="drop", tool="ears.py", tool_version=KVER, method="24 critical bands")
        ks.record(sid, "mfcc.timbre", vector=[float(v) for v in F['mfcc']],
                  window="drop", tool="ears.py", tool_version=KVER, method="13-coeff timbre envelope")
        print(f"  [recorded 5 measurements -> subject #{sid} '{name}' in {ks.path}]")

def _split_opts(a):
    """Pull --record/--db/--kind/--subject flags out of a positional arg list.
    Returns (positional_args, opts_dict). No flags -> opts['record'] is False."""
    pos, opts = [], {'record': False, 'db': None, 'kind': None, 'subject': None}
    i = 0
    while i < len(a):
        t = a[i]
        if t == '--record': opts['record'] = True; i += 1
        elif t == '--db': opts['db'] = a[i+1]; i += 2
        elif t == '--kind': opts['kind'] = a[i+1]; i += 2
        elif t == '--subject': opts['subject'] = a[i+1]; i += 2
        else: pos.append(t); i += 1
    return pos, opts

def main():
    if len(sys.argv)<3:
        print("usage: ears.py ears|describe <wav> [bpm] | dist <a> <b> [bpm] | nearest <wav> <refdir> [bpm]")
        print("       add --record [--db PATH] [--kind reference|render|stem] [--subject NAME] to persist"); return
    c=sys.argv[1]
    args, opts = _split_opts(sys.argv[2:])
    if c=='ears':
        cmd_ears(args)
        if opts['record']:
            x,sr=load(args[0]); F=features(x,sr, float(args[1]) if len(args)>1 else 145.0)
            _record_features(args[0], F, opts)
    elif c=='dist':
        cmd_dist(args)
        if opts['record']:
            bpm=float(args[2]) if len(args)>2 else 145.0
            for p in (args[0], args[1]):
                x,sr=load(p); _record_features(p, features(x,sr,bpm), opts)
    elif c=='describe': cmd_describe(args)
    elif c=='nearest': cmd_nearest(args)
main()
