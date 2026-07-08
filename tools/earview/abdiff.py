#!/usr/bin/env python3
# abdiff — A/B DIFFERENCE imaging on a Bark scale. Given MINE and a REFERENCE
# wav, show where my spectrum differs from the reference: a signed/diverging
# heatmap (I'm louder = red excess, ref louder = blue deficit, white ~ match),
# so "I'm missing 4-8k air" or "I have 200Hz mud" reads at a glance.
#   abdiff <mine.wav> <ref.wav> <out.png>
# Both are level-normalized first so the diff reflects TONAL BALANCE, not gain.
# Deps: numpy, scipy. No ML.
import sys, os, struct, zlib, numpy as np
from scipy.signal import stft, butter, sosfiltfilt

# ---- io (copied from earview.py) ----
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

def write_png(path,rgb):
    H,W,_=rgb.shape; raw=bytearray()
    for y in range(H): raw.append(0); raw.extend(rgb[y].tobytes())
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))
                          +ch(b'IDAT',zlib.compress(bytes(raw),9))+ch(b'IEND',b''))

# diverging colormap: v in -1..1  ->  blue (ref louder / deficit) .. near-white .. red (excess)
def divergent(v):
    v=np.clip(v,-1,1); a=np.abs(v)
    # neutral off-white at 0, saturating toward blue for v<0 and red for v>0
    r=np.where(v>=0, 1.0, 1.0-0.85*a); g=1.0-0.85*a; b=np.where(v<0, 1.0, 1.0-0.85*a)
    return np.clip(np.stack([r,g,b],-1)*255,0,255).astype(np.uint8)

def hz2bark(f): return 13*np.arctan(0.00076*f)+3.5*np.arctan((f/7500.0)**2)

# drop window ~6s, selected by sub-band (one-pole LP ~120Hz) energy — same idea
# as dissect/earview/ears drop pick: the loudest low-end region is the drop.
def drop6(x,sr):
    wl=int(6.0*sr)
    sub=sosfiltfilt(butter(4,120/(sr/2),'lp',output='sos'),x)
    best=-1; s0=0
    for s in range(0,max(1,len(x)-wl),int(0.5*sr)):
        e=np.sum(sub[s:s+wl]**2)
        if e>best: best=e; s0=s
    return x[s0:s0+wl], s0/sr

# average power per 24 Bark bands (linear power, NOT loudness-compressed, so a dB
# ratio is meaningful). returns (power[24], center_hz[24]).
def bark_power(seg,sr):
    f,t,Z=stft(seg,sr,nperseg=4096,noverlap=3072); P=(np.abs(Z)**2).mean(1)
    z=hz2bark(f); pw=np.zeros(24); cen=np.zeros(24)
    for k in range(24):
        m=(z>=k)&(z<k+1)
        if m.any(): pw[k]=P[m].sum(); cen[k]=(f[m]*P[m]).sum()/(P[m].sum()+1e-12)
        else: cen[k]=f[np.argmin(np.abs(z-(k+0.5)))]
    return pw,cen

def main():
    if len(sys.argv)<4: print("usage: abdiff.py <mine.wav> <ref.wav> <out.png>"); return
    mp,rp,out=sys.argv[1],sys.argv[2],sys.argv[3]
    xm,sr=load(mp); xr,_=load(rp)
    sm,tm=drop6(xm,sr); sr_,tr=drop6(xr,sr)
    pm,cen=bark_power(sm,sr); pr,_=bark_power(sr_,sr)
    # LEVEL-NORMALIZE: equal total band energy, so diff = tonal balance not gain
    pm=pm/(pm.sum()+1e-12); pr=pr/(pr.sum()+1e-12)
    diff=10*np.log10((pm+1e-12)/(pr+1e-12))   # dB per band; +=mine louder, -=ref louder
    MAX=12.0                                   # +-12 dB maps to full color saturation
    # ---- render: Y = frequency (bark 0 low at bottom .. 24 high at top), each
    # band a solid horizontal stripe colored by its diff (averaged-column style). ----
    RH=22; W=560; rows=[]
    for k in range(23,-1,-1):                  # top of image = high freq
        col=divergent(np.clip(diff[k]/MAX,-1,1))
        rows.append(np.repeat(col[None,None,:],RH,0).repeat(W,1))
    img=np.vstack(rows)                        # (24*RH, W, 3)
    # Hz gridlines: draw thin dark lines at 100,500,2k,8k
    H=img.shape[0]
    for hz in (100,500,2000,8000):
        zt=hz2bark(hz)
        if 0<=zt<24:
            y=int((24-zt)/24*H)                # bark 24 -> y 0 (top)
            if 0<=y<H: img[max(0,y-1):y+1,:]=np.array([70,70,70],np.uint8)
    write_png(out,img)
    print(f"# abdiff  MINE={os.path.basename(mp)} (drop@{tm:.1f}s)  REF={os.path.basename(rp)} (drop@{tr:.1f}s) -> {out}")
    print("# RED=I'm LOUDER (excess)   BLUE=REFERENCE louder (I'm deficient)   white~match")
    print("# gridlines (dark) at 100Hz, 500Hz, 2kHz, 8kHz.  Y: low freq bottom -> high freq top")
    # ranked actionable text
    idx=np.argsort(diff)                       # ascending: most negative = biggest deficit
    print("  TOP DEFICITS (reference is brighter here — add these):")
    for k in idx[:3]:
        print(f"    {cen[k]:6.0f} Hz   {diff[k]:+5.1f} dB below ref")
    print("  TOP EXCESSES (I'm hotter here — consider cutting):")
    for k in idx[::-1][:3]:
        print(f"    {cen[k]:6.0f} Hz   {diff[k]:+5.1f} dB above ref")
    print("  per-band diff (Hz: dB):")
    print("   "+"  ".join(f"{cen[k]:.0f}:{diff[k]:+.1f}" for k in range(24)))
main()
