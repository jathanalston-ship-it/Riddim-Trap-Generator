#!/usr/bin/env python3
# toms — analyse tuned-tom / tribal-drum character in a section. Isolates the
# tom band, detects hits, and reports per-hit pitch (fundamental), decay,
# tonality (tonal boom vs noisy hit), and pitch-glide, plus the aggregate
# character — so a generation's toms can be compared to a reference's.
#   toms.py <wav> <t0> <dur> [--bpm N] [label]
import sys, numpy as np
import soundfile as sf, librosa
from scipy.signal import butter, sosfiltfilt

def load(p,t0,dur):
    x,sr=sf.read(p,always_2d=True); x=x.mean(1).astype(np.float32)
    if sr!=48000: x=librosa.resample(x,orig_sr=sr,target_sr=48000); sr=48000
    return x[int(t0*sr):int((t0+dur)*sr)], sr

def ac_pitch(seg, sr, fmin, fmax):
    if len(seg) < int(sr/fmin)+2: return 0.0
    w=seg*np.hanning(len(seg)); ac=np.correlate(w,w,'full')[len(w)-1:]
    lo,hi=int(sr/fmax),int(sr/fmin)
    if hi>=len(ac) or hi<=lo: return 0.0
    pk=lo+int(np.argmax(ac[lo:hi])); return sr/pk if pk else 0.0

def analyze(p,t0,dur,bpm,label):
    x,sr=load(p,t0,dur)
    # tom band: 60-600 Hz (tuned low-mid drum), percussive part only
    _,P=librosa.effects.hpss(x)
    tb=sosfiltfilt(butter(4,[60/(sr/2),600/(sr/2)],'bp',output='sos'),P).astype(np.float32)
    oenv=librosa.onset.onset_strength(y=tb,sr=sr,hop_length=256)
    on=librosa.onset.onset_detect(onset_envelope=oenv,sr=sr,hop_length=256,backtrack=True,units='samples')
    win=int(0.30*sr)
    pitches=[]; decays=[]; tonal=[]; glides=[]; cens=[]
    for s in on:
        seg=tb[s:s+win]
        if len(seg)<int(0.06*sr): continue
        if np.sqrt(np.mean(seg**2))<0.5*np.sqrt(np.mean(tb**2)): continue
        f0=ac_pitch(seg[:int(0.12*sr)],sr,50,400)
        if f0>0: pitches.append(f0)
        # decay: 1/e of smoothed envelope after peak
        env=np.abs(seg); a=np.exp(-1/(0.002*sr))
        from scipy.signal import lfilter
        env=lfilter([1-a],[1,-a],env); pk_i=int(np.argmax(env)); pk=env[pk_i]+1e-9
        after=env[pk_i:]; below=np.where(after<pk/np.e)[0]
        decays.append(1000.0*(int(below[0]) if len(below) else len(after))/sr)
        # tonality: harmonic (autocorr peak strength) -> 1 tonal, 0 noisy
        w=seg*np.hanning(len(seg)); ac=np.correlate(w,w,'full')[len(w)-1:]
        ac=ac/(ac[0]+1e-9); lo=int(sr/400); hi=int(sr/50)
        tonal.append(float(np.max(ac[lo:hi])) if hi<len(ac) else 0.0)
        # pitch glide: early vs late fundamental
        fe=ac_pitch(seg[:int(0.03*sr)],sr,50,500); fl=ac_pitch(seg[int(0.06*sr):int(0.15*sr)],sr,50,400)
        if fe>0 and fl>0: glides.append(fe/fl)
        S=np.abs(np.fft.rfft(w)); fr=np.fft.rfftfreq(len(w),1/sr); cens.append(float((fr*S).sum()/(S.sum()+1e-9)))
    def med(a): return float(np.median(a)) if a else 0.0
    def note(f):
        if f<=0: return '--'
        m=int(round(12*np.log2(f/440.0)+69)); N=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']; return N[m%12]+str(m//12-1)
    print(f"# {label}: {len(on)} tom hits in {t0}-{t0+dur}s ({bpm} BPM)")
    print(f"  pitch:     median {med(pitches):.0f}Hz ({note(med(pitches))})  range {min(pitches) if pitches else 0:.0f}-{max(pitches) if pitches else 0:.0f}Hz  ({len(set(round(p/10) for p in pitches))} distinct)")
    print(f"  decay:     median {med(decays):.0f}ms")
    print(f"  tonality:  {med(tonal):.2f}  (1=tuned boom, 0=noisy hit)")
    print(f"  pitchglide:{med(glides):.2f}  (>1 = drops in pitch like a real tom)")
    print(f"  centroid:  {med(cens):.0f}Hz")
    return dict(pitch=med(pitches),decay=med(decays),tonal=med(tonal),glide=med(glides),cen=med(cens),n=len(on))

if __name__=='__main__':
    a=sys.argv[1:]; wav=a[0]; t0=float(a[1]); dur=float(a[2]); bpm=145.0; label=wav
    i=3
    while i<len(a):
        if a[i]=='--bpm': bpm=float(a[i+1]); i+=2
        else: label=a[i]; i+=1
    analyze(wav,t0,dur,bpm,label)
