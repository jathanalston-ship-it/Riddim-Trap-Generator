#!/usr/bin/env python3
# structure — whole-song arrangement analysis: segment a track into its
# sections (intro / build / drop / break / outro) over time and characterize
# each, so a generation can reproduce the full FORM, not just the drop.
#
#   structure.py <wav> [--bpm N] [--json out.json] [--png out.png]
#
# Riddim/dubstep form is defined mostly by the SUB + DRUMS gate: the drop is
# "sub + drums ON at full energy", breaks/intros are "sub/drums OFF or reduced".
# So we classify each ~1 s frame by (sub energy, drum activity, brightness,
# loudness), merge runs into sections, and label the quiet ones by position.
import sys, os, json, struct, zlib, math
import numpy as np

SR = 48000

def log(*a):
    if not QUIET: print(*a)

def load(path):
    import soundfile as sf
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1).astype(np.float32)
    if sr != SR:
        import librosa
        x = librosa.resample(x, orig_sr=sr, target_sr=SR)
    return x, SR

def frame_features(x, sr, hop_s=0.5):
    import librosa
    from scipy.signal import butter, sosfiltfilt
    hop = int(hop_s * sr)
    sub  = sosfiltfilt(butter(4, 120/(sr/2), 'lp', output='sos'), x).astype(np.float32)
    high = sosfiltfilt(butter(4, 6000/(sr/2), 'hp', output='sos'), x).astype(np.float32)
    # drum activity: percussive-onset strength, frame-summed
    oenv = librosa.onset.onset_strength(y=x, sr=sr, hop_length=512)
    oenv_t = librosa.frames_to_time(np.arange(len(oenv)), sr=sr, hop_length=512)
    feats = []
    for s in range(0, len(x) - hop, hop):
        seg = slice(s, s + hop)
        rms  = float(np.sqrt(np.mean(x[seg] ** 2)) + 1e-9)
        subE = float(np.sqrt(np.mean(sub[seg] ** 2)) + 1e-9)
        hiE  = float(np.sqrt(np.mean(high[seg] ** 2)) + 1e-9)
        t0, t1 = s / sr, (s + hop) / sr
        dm = (oenv_t >= t0) & (oenv_t < t1)
        drum = float(oenv[dm].mean()) if dm.any() else 0.0
        feats.append((s / sr, rms, subE, hiE, drum))
    return feats

def classify(x, sr, feats):
    t   = np.array([f[0] for f in feats])
    rms = np.array([f[1] for f in feats]); sub = np.array([f[2] for f in feats])
    hi  = np.array([f[3] for f in feats]); drum = np.array([f[4] for f in feats])
    # normalize to the track's own dynamics
    def nrm(a):
        lo, peak = np.percentile(a, 5), np.percentile(a, 95)
        return np.clip((a - lo) / (peak - lo + 1e-9), 0, 1)
    rn, sn, hn, dn = nrm(rms), nrm(sub), nrm(hi), nrm(drum)
    # per-frame state
    state = []
    for i in range(len(t)):
        if sn[i] > 0.45 and dn[i] > 0.4 and rn[i] > 0.45:
            state.append('drop')
        elif rn[i] > 0.4 and dn[i] > 0.35 and sn[i] <= 0.45:
            state.append('build')      # energy + drums but sub gated (tension)
        else:
            state.append('quiet')
    # median-smooth the state (kill 1-frame flickers)
    sm = list(state)
    for i in range(1, len(sm) - 1):
        if state[i] != state[i-1] and state[i] != state[i+1] and state[i-1] == state[i+1]:
            sm[i] = state[i-1]
    return t, rn, sn, hn, dn, sm

def merge(t, sm, hop_s, min_s=4.0):
    segs = []
    i = 0
    while i < len(sm):
        j = i
        while j < len(sm) and sm[j] == sm[i]: j += 1
        segs.append([t[i], (t[j-1] + hop_s) if j < len(t) else t[-1] + hop_s, sm[i]])
        i = j
    # absorb too-short segments into the previous one
    out = []
    for s in segs:
        if out and (s[1] - s[0]) < min_s and s[2] != 'drop':
            out[-1][1] = s[1]
        else:
            out.append(s)
    # relabel quiet by position: first=intro, last=outro, else break
    quiet_idx = [k for k, s in enumerate(out) if s[2] == 'quiet']
    for k in quiet_idx:
        if k == 0: out[k][2] = 'intro'
        elif k == len(out) - 1: out[k][2] = 'outro'
        else: out[k][2] = 'break'
    return out

def section_stats(t, rn, sn, hn, dn, seg):
    m = (t >= seg[0]) & (t < seg[1])
    if not m.any(): return dict(energy=0, sub=0, drums=0, bright=0)
    return dict(energy=round(float(rn[m].mean()),2), sub=round(float(sn[m].mean()),2),
                drums=round(float(dn[m].mean()),2), bright=round(float(hn[m].mean()),2))

COL = {'intro':(70,90,160),'build':(190,150,40),'drop':(200,60,60),
       'break':(60,140,130),'outro':(90,70,120)}
def write_png(path, t, rn, sn, dn, segs, dur):
    W,H=1000,240; img=np.full((H,W,3),18,np.uint8)
    def X(tt): return int(tt/dur*(W-1))
    # section band (top 60px); segs are section dicts
    for s in segs:
        c=np.array(COL.get(s['label'],(120,120,120)),np.uint8)
        img[8:64, X(s['start']):max(X(s['start'])+1,X(s['end']))] = c
    # curves: energy(white), sub(red), drums(green)
    def curve(a,color,y0,y1):
        for i in range(len(t)-1):
            x0,x1=X(t[i]),X(t[i+1])
            yv=int(y1-(y1-y0)*float(a[i]))
            img[max(y0,min(y1,yv)):y1, x0:max(x0+1,x1)] = color
    curve(rn,(200,200,200),75,150)
    curve(sn,(210,70,70),155,205)
    curve(dn,(70,200,120),210,236)
    _png(path,img)

def _png(path,rgb):
    H,W,_=rgb.shape; raw=bytearray()
    for r in range(H): raw.append(0); raw.extend(rgb[r].tobytes())
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))
                          +ch(b'IDAT',zlib.compress(bytes(raw),9))+ch(b'IEND',b''))

def analyze(path, bpm=None):
    x, sr = load(path)
    dur = len(x)/sr
    feats = frame_features(x, sr)
    t, rn, sn, hn, dn, sm = classify(x, sr, feats)
    segs = merge(t, sm, 0.5)
    sections = []
    for s in segs:
        st = section_stats(t, rn, sn, hn, dn, s)
        sections.append(dict(label=s[2], start=round(s[0],1), end=round(s[1],1),
                             dur=round(s[1]-s[0],1), **st))
    counts = {}
    for s in sections: counts[s['label']] = counts.get(s['label'],0)+1
    return dict(source=os.path.basename(path), duration=round(dur,1),
                bpm=bpm, sections=sections, counts=counts), (t,rn,sn,dn,dur)

def write_struct_profile(path, prof, bpm):
    # Emit an engine StructureProfile (flat JSON) so a generation reproduces the
    # reference's FORM: section lengths in BARS + drop count. Contiguous drop
    # sections are merged into drop clusters (one engine "Drop" each).
    bps = (bpm / 60.0) / 4.0                      # bars per second
    secs = prof['sections']
    def bars_of(dur): return max(1, int(round(dur * bps)))
    # merge contiguous drop runs into clusters
    clusters, cur = [], 0.0
    for s in secs:
        if s['label'] == 'drop': cur += s['dur']
        elif cur > 0: clusters.append(cur); cur = 0.0
    if cur > 0: clusters.append(cur)
    intro  = sum(s['dur'] for s in secs if s['label'] == 'intro')
    brk    = sum(s['dur'] for s in secs if s['label'] == 'break')
    outro  = sum(s['dur'] for s in secs if s['label'] == 'outro')
    dropCount = max(1, len(clusters))
    dropMean  = sum(clusters) / len(clusters) if clusters else 16 / bps
    breakMean = brk / max(1, dropCount - 1) if dropCount > 1 else brk
    out = {
        'introBars': bars_of(intro) if intro > 0 else 0,
        'buildBars': 8,                            # transition builds (engine adds one per drop)
        'dropBars':  bars_of(dropMean),
        'breakBars': bars_of(breakMean) if breakMean > 0 else 0,
        'outroBars': bars_of(outro) if outro > 0 else 0,
        'dropCount': dropCount,
        'present':   1,
    }
    with open(path, 'w') as fh:
        fh.write('{\n' + ',\n'.join(f'  "{k}": {v}' for k, v in out.items()) + '\n}\n')
    return out

def summary(prof):
    lines=[f"# {prof['source']}: {prof['duration']:.0f}s, {len(prof['sections'])} sections  "
           f"{dict(prof['counts'])}"]
    for s in prof['sections']:
        lines.append(f"  {s['start']:6.1f}-{s['end']:6.1f}s  {s['label']:6s}  "
                     f"({s['dur']:4.1f}s)  E={s['energy']:.2f} sub={s['sub']:.2f} "
                     f"drums={s['drums']:.2f} bright={s['bright']:.2f}")
    return '\n'.join(lines)

def main():
    global QUIET
    a=sys.argv[1:]
    if not a: print("usage: structure.py <wav> [--bpm N] [--json out] [--png out] [--quiet]"); return
    wav=a[0]; bpm=None; jout=None; pout=None; spout=None; QUIET=False
    i=1
    while i<len(a):
        if a[i]=='--bpm': bpm=float(a[i+1]); i+=2
        elif a[i]=='--json': jout=a[i+1]; i+=2
        elif a[i]=='--png': pout=a[i+1]; i+=2
        elif a[i]=='--struct-profile-out': spout=a[i+1]; i+=2
        elif a[i]=='--quiet': QUIET=True; i+=1
        else: i+=1
    prof,viz=analyze(wav,bpm)
    log(summary(prof))
    if spout:
        sp=write_struct_profile(spout, prof, bpm or 140.0)
        log(f"# wrote {spout} (engine StructureProfile): {sp}")
    if jout: json.dump(prof,open(jout,'w'),indent=1); log(f"# wrote {jout}")
    if pout:
        t,rn,sn,dn,dur=viz
        write_png(pout,t,rn,sn,dn,prof['sections'],dur); log(f"# wrote {pout}")
    if not jout and not pout: print(json.dumps(prof,indent=1))

QUIET=False
if __name__=='__main__': main()
