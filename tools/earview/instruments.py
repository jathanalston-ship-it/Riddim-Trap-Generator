#!/usr/bin/env python3
# instruments — full per-instrument detection + analysis for a reference track.
# The goal: genuinely understand WHAT each instrument is, HOW it sounds, and HOW
# it's used, so the engine can reproduce a reference's actual instrument set.
#
#   instruments.py <wav> [--bpm N] [--json out.json] [--png out.png] [--quiet]
#
# Pipeline (classical MIR, no ML weights — those hosts are network-blocked;
# librosa + sklearn are enough for a genre with a known instrument palette):
#   1. load + beat/tempo grid
#   2. HPSS -> percussive (drums) vs harmonic (tonal)
#   3. drums: onset-detect the percussive side, classify each hit
#      (kick/snare/clap/hat/tom/perc) by band-energy + centroid + decay + ZCR,
#      then aggregate per class: timbre template, decay, 16-step grid pattern.
#   4. tonal: NMF-decompose the harmonic magnitude spectrogram into components,
#      classify each (sub / growl-bass / mid-lead / stab / pad / high-lead) by
#      frequency range + sustain + roughness + odd/even harmonic ratio, then
#      merge same-class components and read pitch + rhythmic activation.
#   5. emit a structured InstrumentProfile (JSON) + a plain-language summary,
#      and optionally a visualization.
import sys, os, json, struct, zlib, math
import numpy as np

SR = 48000
NOTE = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']

def log(*a):
    if not QUIET: print(*a)

# ---------------------------------------------------------------------------
def load(path):
    import soundfile as sf
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1).astype(np.float32)
    if sr != SR:
        import librosa
        x = librosa.resample(x, orig_sr=sr, target_sr=SR)
    return x, SR

def hz2note(f):
    if f <= 0: return '--'
    m = int(round(12*math.log2(f/440.0)+69))
    return NOTE[m%12]+str(m//12-1)

def band_energy(S, freqs, lo, hi):
    m = (freqs>=lo)&(freqs<hi)
    return float(S[m].sum())

# ---------------------------------------------------------------------------
# DRUMS: NMF-decompose the percussive spectrogram into voices, classify each by
# its spectral template (robust to dense overlapping hits; reliably isolates the
# kick as a low-frequency component even when HPSS sent its sub to the harmonic
# side). Pattern + decay come from each component's activation.
def analyze_drums(P, sr, bpm, n_comp=6):
    import librosa
    from sklearn.decomposition import NMF
    hop = 256                              # fine time resolution for transients
    S = np.abs(librosa.stft(P, n_fft=2048, hop_length=hop))
    freqs = librosa.fft_frequencies(sr=sr, n_fft=2048)
    fs_env = sr/hop
    model = NMF(n_components=n_comp, init='nndsvda', max_iter=300, random_state=0)
    A = model.fit_transform(S.T); W = model.components_.T; A = A.T  # W:freq×k A:k×time
    voices = []
    for k in range(n_comp):
        w = W[:,k]; a = A[k]
        if w.sum() < 1e-6 or a.sum() < 1e-6: continue
        tot = w.sum()+1e-9
        eb = {'sub':band_energy(w,freqs,20,120)/tot,'low':band_energy(w,freqs,120,500)/tot,
              'mid':band_energy(w,freqs,500,2000)/tot,'hi':band_energy(w,freqs,2000,6000)/tot,
              'air':band_energy(w,freqs,6000,24000)/tot}
        cen = float((freqs*(w/tot)).sum())
        cls = classify_drum_nmf(eb, cen)
        voices.append(dict(cls=cls, cen=cen, bands=eb, act=a, prom=float(np.sqrt(np.mean(a**2))),
                           dec_ms=component_decay(a, fs_env), fs_env=fs_env))
    # merge components sharing a class (prominence-weighted)
    out = {}
    byc = {}
    for v in voices: byc.setdefault(v['cls'], []).append(v)
    for cls, vs in byc.items():
        wsum = sum(v['prom'] for v in vs)+1e-9
        act = np.sum([v['act'] for v in vs], axis=0)
        pat = grid_pattern(act, bpm, fs_env)
        out[cls] = dict(
            components=len(vs),
            centroid_hz=round(float(sum(v['cen']*v['prom'] for v in vs)/wsum),1),
            decay_ms=round(float(sum(v['dec_ms']*v['prom'] for v in vs)/wsum),1),
            bands={b:round(float(sum(v['bands'][b]*v['prom'] for v in vs)/wsum),3)
                   for b in vs[0]['bands']},
            density=round(float((act>0.2*act.max()).mean()),2),
            prominence=round(float(np.sqrt(np.mean(act**2))),4),
            pattern16=pat,
        )
    # normalize prominence 0..1
    pm = max((d['prominence'] for d in out.values()), default=1e-9)
    for d in out.values(): d['prominence'] = round(d['prominence']/pm, 3)
    return out

def classify_drum_nmf(eb, cen):
    if eb['sub']+eb['low'] > 0.5 and cen < 320:      return 'kick'
    if eb['air'] > 0.30 or cen > 7000:               return 'hat'
    if eb['low']+eb['mid'] > 0.55 and 150 < cen < 700: return 'tom'
    if eb['mid']+eb['hi'] > 0.45 and 700 <= cen < 5000: return 'snare'
    return 'perc'

def autocorr_pitch(seg, sr, fmin, fmax):
    if len(seg) < int(sr/fmin)+2: return 0.0
    w = seg*np.hanning(len(seg)); ac = np.correlate(w, w, 'full')[len(w)-1:]
    lo, hi = int(sr/fmax), int(sr/fmin)
    if hi >= len(ac) or hi <= lo: return 0.0
    pk = lo+int(np.argmax(ac[lo:hi]))
    return sr/pk if pk else 0.0

def detect_kick(x, sr, bpm):
    # The kick is the sharpest sub-band transient. Onset-detect a 35-160 Hz
    # bandpass of the FULL mix (HPSS may have sent the kick's body to the
    # harmonic side, so don't rely on the percussive stem here).
    import librosa
    from scipy.signal import butter, sosfiltfilt, lfilter
    low = sosfiltfilt(butter(4,[35/(sr/2),160/(sr/2)],'bp',output='sos'), x).astype(np.float32)
    hop = 256
    oenv = librosa.onset.onset_strength(y=low, sr=sr, hop_length=hop)
    on = librosa.onset.onset_detect(onset_envelope=oenv, sr=sr, hop_length=hop,
                                    backtrack=True, units='samples')
    if len(on) < 3: return None
    aE = math.exp(-1/(0.003*sr)); win = int(0.16*sr)
    decs, pitches, drops = [], [], []
    for s in on:
        seg = low[s:s+win]
        if len(seg) < 512: continue
        env = lfilter([1-aE],[1,-aE], np.abs(seg))
        pk_i = int(np.argmax(env)); pk = env[pk_i]+1e-9; after = env[pk_i:]
        below = np.where(after < pk/math.e)[0]
        decs.append(1000.0*(int(below[0]) if len(below) else len(after))/sr)
        f = autocorr_pitch(seg[:win//2], sr, 30, 160)
        if f > 0: pitches.append(f)
        # pitch-drop: fundamental early vs late (EDM kicks click high, thump low)
        fe = autocorr_pitch(seg[:int(0.02*sr)], sr, 40, 300)
        fl = autocorr_pitch(seg[int(0.04*sr):int(0.10*sr)], sr, 30, 160)
        if fe > 0 and fl > 0: drops.append(fe/fl)
    beatlen = 60.0/bpm; pat = np.zeros(16)
    for s in on: pat[int(round((s/sr)/(beatlen/4)))%16] += 1
    pat = pat/(pat.max()+1e-9)
    return dict(count=len(on),
                decay_ms=round(float(np.median(decs)),1) if decs else 0.0,
                pitch_hz=round(float(np.median(pitches)),1) if pitches else 0.0,
                pitch_drop=round(float(np.median(drops)),2) if drops else 1.0,
                density=round(float((pat>0.15).mean()),2),
                pattern16=[round(float(v),2) for v in pat])

def detect_sub(H, sr, bpm):
    # The sustained deep bass fundamental. Bandpass the harmonic stem to 25-130
    # Hz, autocorrelation pitch over the whole drop, and its rhythmic gate.
    from scipy.signal import butter, sosfiltfilt
    b = sosfiltfilt(butter(4,[25/(sr/2),130/(sr/2)],'bp',output='sos'), H).astype(np.float32)
    rms = float(np.sqrt(np.mean(b**2)))
    if rms < 1e-4: return None
    hop = int(0.06*sr); win = int(0.12*sr); pitches=[]; env=[]
    for s in range(0, len(b)-win, hop):
        seg = b[s:s+win]
        if np.sqrt(np.mean(seg**2)) < 0.4*rms: env.append(0.0); continue
        f = autocorr_pitch(seg, sr, 28, 130);
        if f>0: pitches.append(f)
        env.append(float(np.sqrt(np.mean(seg**2))))
    f0 = float(np.median(pitches)) if pitches else 0.0
    act = np.array(env); fs_env = sr/hop
    return dict(fundamental_hz=round(f0,1), note=hz2note(f0),
                level_rms=round(rms,4),
                density=round(float((act>0.2*(act.max()+1e-9)).mean()),2),
                pattern16=grid_pattern(np.repeat(act, 1), bpm, fs_env))

def component_decay(a, fs_env):
    # median post-peak time (ms) for the activation to fall to 1/e, over the
    # component's strong hits — a proxy for the voice's amplitude decay.
    a = np.asarray(a); mx = a.max()+1e-9
    peaks = np.where((a[1:-1] > 0.4*mx) & (a[1:-1] >= a[:-2]) & (a[1:-1] > a[2:]))[0]+1
    decs = []
    for p in peaks[:200]:
        thr = a[p]/math.e; q = p+1
        while q < len(a) and a[q] > thr: q += 1
        decs.append((q-p)/fs_env*1000.0)
    return float(np.median(decs)) if decs else 0.0

# ---------------------------------------------------------------------------
# TONAL: NMF-decompose harmonic side, classify components into instruments.
def analyze_tonal(H, sr, bpm, n_comp=8):
    import librosa
    from sklearn.decomposition import NMF
    hop = 512
    S = np.abs(librosa.stft(H, n_fft=4096, hop_length=hop))
    freqs = librosa.fft_frequencies(sr=sr, n_fft=4096)
    times = librosa.frames_to_time(np.arange(S.shape[1]), sr=sr, hop_length=hop)
    # NMF: S ~= W(freq x k) @ A(k x time)
    model = NMF(n_components=n_comp, init='nndsvda', max_iter=250,
                random_state=0, beta_loss='frobenius')
    W = model.fit_transform(S.T).T if False else None
    A = model.fit_transform(S.T)          # time x k
    W = model.components_.T               # freq x k
    A = A.T                               # k x time
    comps = []
    for k in range(n_comp):
        w = W[:,k]; a = A[k]
        if w.sum() < 1e-6 or a.sum() < 1e-6: continue
        wn = w/(w.sum()+1e-9)
        cen = float((freqs*wn).sum())
        # fundamental via harmonic-product-spectrum (robust to a strong 2nd/3rd
        # harmonic) restricted to a musical bass/mid range, not the lowest bin.
        f0 = hps_fundamental(w, freqs, fmin=30.0, fmax=500.0)
        # spectral spread + flatness (noisy vs tonal)
        spread = float(np.sqrt(((freqs-cen)**2*wn).sum()))
        flat = float(np.exp(np.mean(np.log(w+1e-9))) / (w.mean()+1e-9))
        # odd/even harmonic energy around f0
        odd, even = harmonic_ratio(w, freqs, f0)
        # activation character
        an = a/(a.max()+1e-9)
        active = an > 0.2
        density = float(active.mean())
        # sustain: mean run length of active frames (long -> pad/sustain)
        sustain = float(mean_run(active))
        # roughness: AM of the activation env in the 15-150 Hz-equivalent (use
        # activation sampled at sr/hop; measure normalized modulation energy)
        rough = activation_roughness(a, sr/hop)
        prom = float(np.sqrt(np.mean(a**2)))
        comps.append(dict(k=k, f0=f0, centroid=cen, spread=spread, flat=flat,
                          odd=odd, even=even, density=density, sustain=sustain,
                          rough=rough, prom=prom, act=a, basis=w, freqs=freqs))
    # normalize prominence
    pmax = max((c['prom'] for c in comps), default=1e-9)
    for c in comps: c['prom_n'] = c['prom']/(pmax+1e-9)
    # classify + merge
    for c in comps: c['type'] = classify_tonal(c)
    return merge_tonal(comps, bpm, sr/hop), comps

def hps_fundamental(w, freqs, fmin=30.0, fmax=500.0):
    # Harmonic product spectrum: multiply downsampled copies so the true
    # fundamental (whose harmonics all align) wins over a loud upper partial.
    df = freqs[1]-freqs[0]
    if df <= 0: return float(freqs[np.argmax(w)])
    hps = w.copy()
    for h in (2,3,4):
        ds = w[::h]
        hps[:len(ds)] *= ds
    lo = max(1,int(fmin/df)); hi = min(len(hps),int(fmax/df))
    if hi <= lo: return float(freqs[np.argmax(w)])
    return float(freqs[lo+int(np.argmax(hps[lo:hi]))])

def harmonic_ratio(w, freqs, f0):
    if f0 <= 0: return 0.5, 0.5
    odd = even = 0.0
    for n in range(1, 12):
        fn = f0*n
        if fn >= freqs[-1]: break
        bidx = np.argmin(np.abs(freqs-fn))
        e = float(w[max(0,bidx-1):bidx+2].sum())
        if n % 2 == 1: odd += e
        else: even += e
    s = odd+even+1e-9
    return round(odd/s,3), round(even/s,3)

def mean_run(mask):
    runs=[]; c=0
    for v in mask:
        if v: c+=1
        elif c: runs.append(c); c=0
    if c: runs.append(c)
    return float(np.mean(runs)) if runs else 0.0

def activation_roughness(a, fs_env):
    # normalized amplitude-modulation energy of the activation envelope in the
    # 15-150 Hz roughness band (perceived gnarl), matching the engine's measure.
    a = a - a.mean()
    if np.sqrt(np.mean(a**2)) < 1e-9: return 0.0
    A = np.abs(np.fft.rfft(a*np.hanning(len(a))))
    f = np.fft.rfftfreq(len(a), 1/fs_env)
    band = (f>=15)&(f<=150); tot=(f>=1)
    return round(float(A[band].sum()/(A[tot].sum()+1e-9)),3)

def classify_tonal(c):
    cen, f0, sus, rough, flat = c['centroid'], c['f0'], c['sustain'], c['rough'], c['flat']
    if f0 < 90 and cen < 250:
        return 'sub'
    # Riddim growls scream well up into the low-treble; the defining trait is
    # a square-ish (odd-harmonic) or ROUGH (amplitude-modulated) timbre — not a
    # low centroid. NMF splits the growl across formant components, so gate on
    # TIMBRE not a fundamental (HPS on a formant-heavy basis is unreliable).
    if cen < 4000 and (c['odd'] > 0.55 or rough > 0.15):
        return 'growl'
    if cen < 700 and f0 < 200:
        return 'bass'           # clean low bass
    if sus > 25 and flat < 0.2 and cen < 4000:
        return 'pad'            # long sustained tonal
    if cen > 3500:
        return 'lead_hi'        # bright lead
    if c['density'] < 0.25 and cen >= 700:
        return 'stab'           # sparse mid hit
    return 'lead'

def merge_tonal(comps, bpm, fs_env):
    out = {}
    byt = {}
    for c in comps: byt.setdefault(c['type'], []).append(c)
    for t, cs in byt.items():
        wsum = sum(c['prom'] for c in cs)+1e-9
        wavg = lambda key: round(float(sum(c[key]*c['prom'] for c in cs)/wsum),3)
        # merged activation for a pattern
        act = np.sum([c['act']*c['prom'] for c in cs], axis=0)
        pat = grid_pattern(act, bpm, fs_env)
        f0 = float(np.median([c['f0'] for c in cs]))
        out[t] = dict(
            components=len(cs),
            fundamental_hz=round(f0,1), note=hz2note(f0),
            centroid_hz=round(wavg('centroid'),1),
            odd_ratio=wavg('odd'), roughness=wavg('rough'),
            sustain_frames=round(wavg('sustain'),1),
            density=wavg('density'),
            prominence=round(float(sum(c['prom_n'] for c in cs)/len(cs)),3),
            pattern16=pat,
        )
    return out

def grid_pattern(act, bpm, sr_env):
    # fold the activation onto a 16-step bar grid (relative strength per step)
    steplen = (60.0/bpm)/4
    pat = np.zeros(16); cnt=np.zeros(16)
    for i,v in enumerate(act):
        step=int(round((i/sr_env)/steplen))%16
        pat[step]+=v; cnt[step]+=1
    pat = pat/(cnt+1e-9); pat=pat/(pat.max()+1e-9)
    return [round(float(x),2) for x in pat]

# ---------------------------------------------------------------------------
def plain_language(profile):
    lines=[]
    dr = profile['drums']; to = profile['tonal']
    lines.append(f"TEMPO ~{profile['bpm']} BPM.  Detected {len(dr)} drum voice(s), {len(to)} tonal instrument(s).")
    for cls, d in sorted(dr.items(), key=lambda kv:-kv[1]['prominence']):
        lines.append(f"  DRUM {cls.upper():6s}: centroid {d['centroid_hz']:.0f}Hz, "
                     f"decay {d['decay_ms']:.0f}ms, density {d['density']:.2f}, prom {d['prominence']:.2f}")
    for t, d in sorted(to.items(), key=lambda kv:-kv[1]['prominence']):
        extra=''
        if t in ('growl','bass','sub'): extra=f", root {d['note']} ({d['fundamental_hz']:.0f}Hz)"
        lines.append(f"  TONAL {t.upper():7s}: centroid {d['centroid_hz']:.0f}Hz, "
                     f"rough {d['roughness']:.2f}, odd {d['odd_ratio']:.2f}, "
                     f"prom {d['prominence']:.2f}{extra}")
    return '\n'.join(lines)

# ---------------------------------------------------------------------------
def write_png(path, comps, drums, profile):
    # simple stacked visualization: drum grid + tonal basis spectra
    H,W=520,900
    img=np.full((H,W,3),18,np.uint8)
    def hot(v):
        v=max(0,min(1,v)); r=min(1,1.7*v); g=max(0,min(1,1.7*v-0.7)); b=max(0,min(1,2.2*v-1.2))
        return np.array([r,g,b])*255
    # top: drum patterns (rows per class, 16 cols)
    y=10; classes=list(drums.keys())
    for ci,cls in enumerate(classes):
        pat=drums[cls]['pattern16']
        for st in range(16):
            x0=40+st*46; c=hot(pat[st])
            img[y:y+26, x0:x0+42]=c
        y+=32
    # bottom: tonal basis spectra (log-freq curves) as stacked bands
    y=max(y+10,220)
    for t,d in sorted(profile['tonal'].items(), key=lambda kv:-kv[1]['prominence']):
        val=d['prominence']
        for st in range(16):
            x0=40+st*46; c=hot(d['pattern16'][st]*val)
            img[y:y+22, x0:x0+42]=c
        y+=28
        if y>H-30: break
    _png(path,img)

def _png(path,rgb):
    H,W,_=rgb.shape; raw=bytearray()
    for r in range(H): raw.append(0); raw.extend(rgb[r].tobytes())
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))
                          +ch(b'IDAT',zlib.compress(bytes(raw),9))+ch(b'IEND',b''))

# ---------------------------------------------------------------------------
def loudest_drop(x, sr, secs=40.0):
    # sub-band (one-pole LP ~120Hz) energy picks the DROP, not a bright break —
    # matching the other earview tools' drop selection. Analyze the instrument
    # set that defines the song, and keep runtime bounded.
    n=len(x); wl=min(n,int(secs*sr))
    if n<=wl: return x
    a=math.exp(-2*math.pi*120.0/sr); z=0.0; lo=np.empty(n,np.float32)
    for i in range(0,n,1):  # vectorized one-pole below
        break
    # vectorized one-pole low-pass via lfilter
    from scipy.signal import lfilter
    lo=lfilter([1-a],[1,-a],np.abs(x))
    hop=int(0.5*sr); best=-1; s0=0
    for s in range(0,n-wl+1,hop):
        e=float(np.dot(lo[s:s+wl:32],lo[s:s+wl:32]))
        if e>best: best=e; s0=s
    return x[s0:s0+wl]

def analyze(path, bpm=None, drop_secs=40.0):
    import librosa
    x, sr = load(path)
    if drop_secs and len(x) > drop_secs*sr:
        x = loudest_drop(x, sr, drop_secs)
    if bpm is None:
        tempo, beats = librosa.beat.beat_track(y=x, sr=sr, units='time')
        bpm = float(np.atleast_1d(tempo)[0])
        beat_times = beats
    else:
        beat_times = np.arange(0, len(x)/sr, 60.0/bpm)
    log(f"# {os.path.basename(path)}: tempo {bpm:.1f} BPM, {len(x)/sr:.0f}s")
    H, P = librosa.effects.hpss(x)
    drums = analyze_drums(P, sr, bpm)
    kick = detect_kick(x, sr, bpm)
    if kick:                                # dedicated low-band kick voice
        drums['kick'] = {**kick, 'centroid_hz': kick['pitch_hz'], 'prominence': 1.0,
                         'bands': {'sub':0.6,'low':0.3,'mid':0.1,'hi':0.0,'air':0.0}}
    sub = detect_sub(H, sr, bpm)
    tonal, comps = analyze_tonal(H, sr, bpm)
    if sub: tonal['sub'] = {**sub, 'centroid_hz': sub['fundamental_hz'],
                            'odd_ratio': 0.0, 'roughness': 0.0, 'prominence': 1.0}
    profile = dict(source=os.path.basename(path), bpm=round(float(bpm),1),
                   drums=drums, tonal=tonal)
    return profile, comps

def clampf(v, lo, hi): return float(max(lo, min(hi, v)))

def write_drum_profile(path, profile):
    # Emit a JSON whose keys match rtg::DrumProfile::loadFromFile so the engine
    # can steer its kick/snare/hat synthesis toward the DETECTED drums with no
    # new parsing. Only fields whose measurement semantics genuinely match are
    # written (others fall back to the engine's builtin reference); values are
    # clamped to the profiler's documented sane ranges so a noisy detection can
    # never push the synth somewhere ugly.
    d = profile['drums']; out = {}
    if 'kick' in d:
        k = d['kick']
        if k.get('pitch_hz', 0) > 0: out['kickBodyHz'] = round(clampf(k['pitch_hz'], 40, 70), 2)
        out['kickDecayMs'] = round(clampf(k['decay_ms'], 45, 180), 1)
    if 'snare' in d:
        out['snareCrackDecayMs'] = round(clampf(d['snare']['decay_ms'], 56, 193), 1)
    if 'hat' in d:
        h = d['hat']
        out['hatCentroidHz'] = round(clampf(h['centroid_hz'], 4000, 16000), 1)
        out['hatDecayMs']    = round(clampf(h['decay_ms'], 37, 120), 1)
        out['hatDensityPerBeat'] = round(clampf(h['density']*4.0, 1.6, 3.6), 2)
    # 16-step rhythmic accent maps ("how they're used"). The engine biases the
    # chug/hat hit probabilities toward these so generations follow the
    # reference's actual rhythm. Growl = the signature chug; hat = the top.
    # PHASE-ALIGN to the downbeat first: patterns are folded from the drop
    # window's arbitrary start, so rotate every map so the strongest KICK step
    # (the downbeat) lands on index 0 — matching the composer's bar grid.
    def rot(p, k): return [p[(i + k) % 16] for i in range(16)]
    anchor = 0
    if 'kick' in d and 'pattern16' in d['kick']:
        anchor = int(np.argmax(d['kick']['pattern16']))
    g = profile['tonal'].get('growl')
    if g and 'pattern16' in g:
        for i, v in enumerate(rot(g['pattern16'], anchor)): out[f'growlPattern{i}'] = round(float(v), 2)
    if 'hat' in d and 'pattern16' in d['hat']:
        for i, v in enumerate(rot(d['hat']['pattern16'], anchor)): out[f'hatPattern{i}'] = round(float(v), 2)
    out['refCount'] = 1
    with open(path, 'w') as fh:
        fh.write('{\n' + ',\n'.join(f'  "{k}": {v}' for k, v in out.items()) + '\n}\n')
    return out

def main():
    global QUIET
    args = sys.argv[1:]
    if not args:
        print("usage: instruments.py <wav> [--bpm N] [--json out] [--png out] "
              "[--drum-profile-out out] [--quiet]"); return
    wav=args[0]; bpm=None; jout=None; pout=None; dpout=None; QUIET=False
    i=1
    while i < len(args):
        if args[i]=='--bpm': bpm=float(args[i+1]); i+=2
        elif args[i]=='--json': jout=args[i+1]; i+=2
        elif args[i]=='--png': pout=args[i+1]; i+=2
        elif args[i]=='--drum-profile-out': dpout=args[i+1]; i+=2
        elif args[i]=='--quiet': QUIET=True; i+=1
        else: i+=1
    profile, comps = analyze(wav, bpm)
    log(plain_language(profile))
    if jout:
        json.dump(profile, open(jout,'w'), indent=1)
        log(f"# wrote {jout}")
    if pout:
        write_png(pout, comps, profile['drums'], profile); log(f"# wrote {pout}")
    if dpout:
        emitted = write_drum_profile(dpout, profile)
        log(f"# wrote {dpout} (engine DrumProfile): {emitted}")
    if not jout and not pout and not dpout:
        print(json.dumps(profile, indent=1))

QUIET=False
if __name__=='__main__': main()
