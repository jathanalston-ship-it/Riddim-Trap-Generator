#!/usr/bin/env python3
# build — dissect a track's BUILD-UP: how the drums ramp tension into the drop.
# Focuses on drum USAGE over the bars before a drop: which voices play, the
# onset-density ramp, roll acceleration (subdivision speed-up), riser presence,
# and the pre-drop gap.
#
#   build.py <wav> <drop_start_s> [--bpm N] [--bars N] [--png out.png]
import sys, os, struct, zlib, math
import numpy as np

SR = 48000
def log(*a):
    if not QUIET: print(*a)

def load(path):
    import soundfile as sf
    x, sr = sf.read(path, always_2d=True); x = x.mean(1).astype(np.float32)
    if sr != SR:
        import librosa; x = librosa.resample(x, orig_sr=sr, target_sr=SR)
    return x, SR

def band(S, f, lo, hi): return float(S[(f>=lo)&(f<hi)].sum())

def classify(seg, sr):
    w = seg*np.hanning(len(seg)); S = np.abs(np.fft.rfft(w)); f = np.fft.rfftfreq(len(w),1/sr)
    tot = S.sum()+1e-9
    sub,low,mid,hi,air = (band(S,f,20,120)/tot, band(S,f,120,500)/tot,
                          band(S,f,500,2000)/tot, band(S,f,2000,6000)/tot, band(S,f,6000,20000)/tot)
    cen = float((f*S).sum()/tot)
    if sub+low>0.5 and cen<320: return 'kick'
    if air>0.30 or cen>7000:    return 'hat'
    if low+mid>0.55 and 150<cen<700: return 'tom'
    if mid+hi>0.45 and 700<=cen<5000: return 'snare'
    return 'perc'

def analyze(path, drop_s, bpm, bars=16):
    import librosa
    from scipy.signal import butter, sosfiltfilt
    x, sr = load(path)
    beat = 60.0/bpm; barlen = 4*beat
    t0 = max(0.0, drop_s - bars*barlen)
    seg = x[int(t0*sr):int(drop_s*sr)]
    H, P = librosa.effects.hpss(seg)
    hop = 256
    oenv = librosa.onset.onset_strength(y=P, sr=sr, hop_length=hop)
    on = librosa.onset.onset_detect(onset_envelope=oenv, sr=sr, hop_length=hop, backtrack=True, units='samples')
    win = int(0.06*sr)
    hits = []
    for s in on:
        sg = P[s:s+win]
        if len(sg) < 256: continue
        hits.append((t0 + s/sr, classify(sg, sr)))
    # riser: rising broadband/high-freq sustained energy over the build
    hb = sosfiltfilt(butter(4, 2000/(sr/2),'hp',output='sos'), seg).astype(np.float32)
    nwin = int(0.25*sr); riser = np.array([np.sqrt(np.mean(hb[i:i+nwin]**2)) for i in range(0,len(hb)-nwin,nwin)])
    riser = riser/(riser.max()+1e-9)
    # per-bar breakdown
    log(f"# BUILD of {os.path.basename(path)}: {bars} bars before drop @ {drop_s:.1f}s ({bpm} BPM)")
    log(f"# {len(hits)} drum onsets in the build window ({t0:.1f}-{drop_s:.1f}s)")
    voices = {}
    for _,c in hits: voices[c]=voices.get(c,0)+1
    log("# voice mix: " + ", ".join(f"{k}:{v}" for k,v in sorted(voices.items(),key=lambda x:-x[1])))
    rows = []
    for b in range(bars):
        b0, b1 = t0+b*barlen, t0+(b+1)*barlen
        bh = [c for tt,c in hits if b0<=tt<b1]
        vc = {}
        for c in bh: vc[c]=vc.get(c,0)+1
        # roll detection: mean inter-onset interval this bar (smaller = faster)
        bt = sorted(tt for tt,c in hits if b0<=tt<b1)
        ioi = np.diff(bt).mean() if len(bt)>1 else barlen
        subdiv = beat/ioi if ioi>0 else 0     # onsets per beat
        rmask = (np.arange(len(riser))*nwin/sr + t0)
        rlev = float(riser[(rmask>=b0)&(rmask<b1)].mean()) if ((rmask>=b0)&(rmask<b1)).any() else 0.0
        rows.append((b, len(bh), subdiv, rlev, vc))
    log("#  bar  onsets  onsets/beat  riser  voices")
    for b,n,sd,rl,vc in rows:
        vs = " ".join(f"{k}:{v}" for k,v in sorted(vc.items(),key=lambda x:-x[1]))
        bar_glyph = '#'*int(round(sd*2))
        log(f"   {b:3d}  {n:4d}   {sd:5.1f} {bar_glyph:12s} {rl:.2f}   {vs}")
    # summary signals
    dens = np.array([r[1] for r in rows]); sd = np.array([r[2] for r in rows])
    ramp = np.polyfit(np.arange(bars), dens, 1)[0] if bars>1 else 0
    accel = np.polyfit(np.arange(bars), sd, 1)[0] if bars>1 else 0
    last2 = dens[-2:].mean(); early = dens[:max(1,bars//2)].mean()
    gap = dens[-1] < 0.4*early
    log(f"# density ramp/bar: {ramp:+.1f}   subdivision accel/bar: {accel:+.2f} onsets/beat")
    log(f"# final-bar drop-out (pre-drop gap): {'YES' if gap else 'no'}  (last {dens[-1]:.0f} vs early ~{early:.0f})")
    log(f"# peak subdivision: {sd.max():.1f} onsets/beat at bar {int(np.argmax(sd))}")
    return rows, riser

def main():
    global QUIET; QUIET=False
    a=sys.argv[1:]
    if len(a)<2: print("usage: build.py <wav> <drop_start_s> [--bpm N] [--bars N]"); return
    wav=a[0]; drop=float(a[1]); bpm=145.0; bars=16
    i=2
    while i<len(a):
        if a[i]=='--bpm': bpm=float(a[i+1]); i+=2
        elif a[i]=='--bars': bars=int(a[i+1]); i+=2
        elif a[i]=='--quiet': QUIET=True; i+=1
        else: i+=1
    analyze(wav, drop, bpm, bars)

QUIET=False
if __name__=='__main__': main()
