#!/usr/bin/env python3
# Deep riddim analyzer — tempo, key, structure, and the "math" of the drop:
# sub pitch track, per-band onset grid snapped to 16ths, element ratios.
import sys, struct, numpy as np
from scipy.signal import stft, butter, sosfiltfilt, find_peaks

def load_wav(path):
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

def band(x,sr,lo,hi):
    sos=[]
    if lo>20: sos.append(butter(4,lo/(sr/2),'hp',output='sos'))
    if hi<sr/2*0.98: sos.append(butter(4,hi/(sr/2),'lp',output='sos'))
    y=x
    for s in sos: y=sosfiltfilt(s,y)
    return y

def onset_env(x,sr,lo,hi,hopHz=200):
    bx=band(x,sr,lo,hi); w=int(0.006*sr)
    e=np.convolve(np.abs(bx),np.ones(w)/w,'same'); le=np.log(e+1e-4)
    f=np.diff(le,prepend=le[0]); f[f<0]=0
    ds=max(1,int(sr/hopHz)); return f[::ds], hopHz

def tempo(x,sr):
    # broadband + low-band flux, autocorrelate, octave-correct into 130-160 (riddim)
    f,hop=onset_env(x,sr,30,8000); f=f-f.mean()
    ac=np.correlate(f,f,'full')[len(f)-1:]
    cand=[]
    for bpm in np.arange(60,200,0.1):
        lag=(60.0/bpm)*hop
        i0=int(lag); frac=lag-i0
        if i0+1<len(ac):
            v=ac[i0]*(1-frac)+ac[i0+1]*frac; cand.append((bpm,v))
    cand.sort(key=lambda t:-t[1])
    top=cand[0][0]
    # fold to riddim range 135-160
    b=top
    while b<130: b*=2
    while b>160: b/=2
    return b, top

CHROMA=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
# Krumhansl-Schmuckler major/minor profiles
KMAJ=np.array([6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88])
KMIN=np.array([6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17])
def key_detect(x,sr):
    # chroma from low-mid (bass carries the key in riddim); fold to pitch classes
    f,t,Z=stft(x,sr,nperseg=8192,noverlap=6144); P=np.abs(Z)
    freqs=f; chroma=np.zeros(12)
    for k,fr in enumerate(freqs):
        if fr<30 or fr>2000: continue
        pc=int(round(12*np.log2(fr/440.0)+69))%12
        chroma[pc]+=P[k].sum()
    chroma/=(chroma.sum()+1e-9)
    best=None
    for tonic in range(12):
        for prof,mode in [(KMAJ,'maj'),(KMIN,'min')]:
            pr=np.roll(prof,tonic); pr=pr/pr.sum()
            corr=np.corrcoef(chroma,pr)[0,1]
            if best is None or corr>best[0]: best=(corr,CHROMA[tonic],mode)
    return best[1]+' '+best[2], chroma

def structure(x,sr,win=1.0):
    hop=int(win*sr); rms=[]
    for s in range(0,len(x)-hop,hop):
        rms.append(np.sqrt(np.mean(x[s:s+hop]**2)))
    rms=np.array(rms); db=20*np.log10(rms+1e-6)
    return db

def sub_pitch_track(x,sr,t0,t1,hopHz=100):
    # track the dominant sub frequency 30-140 Hz over [t0,t1] via zero-lag FFT peak
    seg=x[int(t0*sr):int(t1*sr)]; sb=band(seg,sr,28,150)
    win=int(sr*0.06); hop=int(sr/hopHz); out=[]
    for s in range(0,len(sb)-win,hop):
        w=sb[s:s+win]*np.hanning(win)
        sp=np.abs(np.fft.rfft(w,4*win)); fr=np.fft.rfftfreq(4*win,1/sr)
        m=(fr>=28)&(fr<=150);
        if sp[m].max()<1e-4: out.append((s/sr,0,0)); continue
        pk=fr[m][np.argmax(sp[m])]; amp=sp[m].max()
        out.append((s/sr,pk,amp))
    return out

def hz_to_note(f):
    if f<=0: return '--'
    n=round(12*np.log2(f/440.0)+69);
    return CHROMA[int(n)%12]+str(int(n)//12-1)

if __name__=='__main__':
    path=sys.argv[1]; x,sr=load_wav(path)
    bpm,raw=tempo(x,sr); key,chroma=key_detect(x,sr)
    print(f"FILE {path}  dur {len(x)/sr:.1f}s")
    print(f"TEMPO {bpm:.2f} BPM (raw peak {raw:.2f})   beat={60/bpm*1000:.1f}ms  16th={60/bpm/4*1000:.1f}ms")
    print(f"KEY  {key}")
    print("CHROMA "+"  ".join(f"{c}{v*100:.0f}" for c,v in sorted(zip(CHROMA,chroma),key=lambda t:-t[1])[:5]))
    db=structure(x,sr);
    print(f"STRUCTURE (1s RMS dB, {len(db)}s):")
    # print a coarse energy map every 4s
    line=''
    for i in range(0,len(db),4):
        v=db[i]; c=' ' if v<-30 else ('.' if v<-20 else ('o' if v<-14 else '#'))
        line+=c
    print('  '+line)
    print('  (each char=4s; #=loud drop  o=mid  .=quiet  space=silent)')
