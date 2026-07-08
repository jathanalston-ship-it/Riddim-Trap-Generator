#!/usr/bin/env python3
# growlscope — isolate a growl stem and render a HIGH-RES perceptual view for
# visual (multimodal) judgement: log-freq spectrogram (80-8000 Hz) with fine
# time resolution so formant MOVEMENT is visible, + a spectral-envelope-over-
# time panel (the "talking" formants) + a mean harmonic spectrum.
import sys, struct, zlib, numpy as np
import soundfile as sf, librosa
from scipy.signal import butter, sosfiltfilt

def load(p, t0, dur):
    x,sr=sf.read(p, always_2d=True); x=x.mean(1).astype(np.float32)
    if sr!=48000: x=librosa.resample(x,orig_sr=sr,target_sr=48000); sr=48000
    return x[int(t0*sr):int((t0+dur)*sr)], sr

def growl(x, sr):
    H,_=librosa.effects.hpss(x)                       # tonal part
    return sosfiltfilt(butter(4,[150/(sr/2),8000/(sr/2)],'bp',output='sos'),H).astype(np.float32)

def logspec(x, sr, fmin=80, fmax=8000, H=240, W=560):
    S=np.abs(librosa.stft(x, n_fft=4096, hop_length=256))
    f=librosa.fft_frequencies(sr=sr,n_fft=4096); mag=20*np.log10(S+1e-6)
    lf=np.logspace(np.log10(fmin),np.log10(fmax),H)
    idx=[np.argmin(np.abs(f-ff)) for ff in lf]
    M=mag[idx]                                        # H x T
    xs=(np.arange(W)*(M.shape[1]/W)).astype(int); M=M[:,xs]
    lo,hi=np.percentile(M,4),np.percentile(M,99.5)
    return np.clip((M-lo)/(hi-lo+1e-9),0,1)[::-1]

def envpanel(x, sr, H=120, W=560):
    # spectral envelope (cepstral-smoothed) over time -> see formants move
    S=np.abs(librosa.stft(x,n_fft=2048,hop_length=256))+1e-6
    logS=np.log(S); c=np.fft.irfft(logS,axis=0); c[24:]=0     # low-quefrency = envelope
    env=np.real(np.fft.rfft(c,axis=0)); env=np.exp(env)
    f=librosa.fft_frequencies(sr=sr,n_fft=2048)
    lf=np.logspace(np.log10(120),np.log10(6000),H); idx=[np.argmin(np.abs(f-ff)) for ff in lf]
    E=env[idx]; xs=(np.arange(W)*(E.shape[1]/W)).astype(int); E=E[:,xs]
    E=E/(E.max(0,keepdims=True)+1e-9)                 # per-frame normalize -> formant shape
    return E[::-1]

def hot(v):
    v=np.clip(v,0,1); r=np.clip(1.6*v,0,1); g=np.clip(1.7*v-0.6,0,1); b=np.clip(2.3*v-1.4,0,1)+0.4*np.clip(1-abs(v-0.3)*4,0,1)
    return np.clip(np.stack([r,g,np.clip(b,0,1)],-1)*255,0,255).astype(np.uint8)

def png(path,rgb):
    H,W,_=rgb.shape; raw=bytearray()
    for y in range(H): raw.append(0); raw.extend(rgb[y].tobytes())
    ch=lambda t,d: struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(bytes(raw),9))+ch(b'IEND',b''))

def panel(p, t0, dur, label):
    x,sr=load(p,t0,dur); g=growl(x,sr)
    spec=hot(logspec(g,sr)); env=hot(envpanel(g,sr))
    sep=np.full((4,spec.shape[1],3),np.array([200,200,60],np.uint8))
    lab=np.full((14,spec.shape[1],3),np.array([25,25,30],np.uint8))
    return np.vstack([lab,spec,sep,env])

if __name__=='__main__':
    # argv: out.png  wav1 t0 dur label1  wav2 t0 dur label2
    out=sys.argv[1]; a=sys.argv[2:]
    panels=[]
    for i in range(0,len(a),4):
        panels.append(panel(a[i],float(a[i+1]),float(a[i+2]),a[i+3]))
        panels.append(np.full((8,panels[0].shape[1],3),np.array([80,80,90],np.uint8)))
    png(out,np.vstack(panels[:-1]))
    print("wrote",out)
