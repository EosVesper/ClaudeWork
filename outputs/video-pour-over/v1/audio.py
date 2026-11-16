"""练习 5 的声音：代码合成一段 100 BPM 的轻快配乐 + 几个动作音效 -> mix.m4a
  python3 audio.py
鼓、贝斯、电钢琴和弦、拨弦旋律都按 timing.py 的拍子排，所以画面切换一定在拍点上。
"""
import subprocess, wave
import numpy as np
from timing import BEAT, ST, TOTAL

SR = 48000
N = int(SR * (TOTAL + 0.5))
t = np.arange(N) / SR
rng = np.random.default_rng(5)
mix = np.zeros((2, N))

def add(buf, at, w, vol=1.0, pan=0.5):
    i0 = int(at * SR)
    if i0 >= N: return
    w = w[:N - i0]
    buf[0, i0:i0 + len(w)] += w * vol * (1 - pan) * 2 ** 0.5
    buf[1, i0:i0 + len(w)] += w * vol * pan * 2 ** 0.5

def hz(semi): return 261.63 * 2 ** (semi / 12)          # 0 = 中央 C
def tt(d): return np.arange(int(d * SR)) / SR
def lowpass(x, fc):
    F = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR)
    return np.fft.irfft(F / (1 + (f / fc) ** 4), len(x))
def highpass(x, fc):
    F = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR)
    return np.fft.irfft(F / (1 + (fc / np.maximum(f, 1)) ** 4), len(x))

# ---------- 鼓 ----------
def kick():
    x = tt(0.3); return np.sin(2 * np.pi * (48 * x + 70 * (1 - np.exp(-x * 30)) / 30)) * np.exp(-x * 13)
def snare():
    x = tt(0.2); return (highpass(rng.standard_normal(len(x)), 1500) * np.exp(-x * 26) * 0.7
                         + np.sin(2 * np.pi * 190 * x) * np.exp(-x * 30) * 0.5)
def hat(open_=False):
    x = tt(0.15 if open_ else 0.05); return highpass(rng.standard_normal(len(x)), 7000) * np.exp(-x * (25 if open_ else 90))
def epiano(semi, d):
    x = tt(d); f = hz(semi)
    return (np.sin(2 * np.pi * f * x + 1.2 * np.sin(2 * np.pi * f * x) * np.exp(-x * 5))
            * np.exp(-x * 1.6) * np.clip(x / 0.005, 0, 1) * np.clip((d - x) / 0.05, 0, 1))
def pluck(semi):
    x = tt(0.5); f = hz(semi)
    return (np.sin(2 * np.pi * f * x) + 0.4 * np.sin(2 * np.pi * 2 * f * x) * np.exp(-x * 9)) * np.exp(-x * 7) * np.clip(x / 0.003, 0, 1)
def bass(semi, d):
    x = tt(d); f = hz(semi - 24)
    return (np.sin(2 * np.pi * f * x) + 0.3 * np.sin(2 * np.pi * 2 * f * x)) * np.exp(-x * 2.5) * np.clip(x / 0.005, 0, 1) * np.clip((d - x) / 0.03, 0, 1)

BARS = int(round(TOTAL / (4 * BEAT)))                       # 8 小节
# 和弦：Cmaj7 - Am7 - Fmaj7 - G（每小节一个，循环）
CH = [(0, [0, 4, 7, 11]), (-3, [-3, 0, 4, 7]), (-7, [-7, -3, 0, 4]), (-5, [-5, -1, 2, 7])]
MEL = [[7, None, 4, 7, 9, None, 7, 4], [4, None, 0, 4, 7, None, 4, 0],
       [5, None, 9, 12, 9, None, 5, 4], [2, 7, None, 11, 14, None, 11, 7]]
for b in range(BARS):
    t0 = b * 4 * BEAT
    root, notes = CH[b % 4]
    last = b == BARS - 1
    # 电钢琴：每小节第 1 拍和第 2 拍半各弹一次；最后一小节只弹一次让它自然收尾
    for at, d in ([(0, 2.4)] if last else [(0, 0.85), (1.5 * BEAT, 1.4)]):
        for k, s in enumerate(notes):
            add(mix, t0 + at + 0.008 * k, epiano(s, d), 0.11, 0.35 + 0.1 * k)
    if b == 0:                                              # 第 1 小节（C1）只有和弦，鼓从第 2 小节进
        add(mix, t0 + 3.5 * BEAT, hat(True), 0.12, 0.6); continue
    if last:
        add(mix, t0, kick(), 0.75); add(mix, t0, bass(root, 2.2), 0.42); continue
    for q in range(4):
        if q in (0, 2): add(mix, t0 + q * BEAT, kick(), 0.75)
        if q in (1, 3): add(mix, t0 + q * BEAT, snare(), 0.30)
        add(mix, t0 + q * BEAT, hat(), 0.10, 0.62)
        add(mix, t0 + (q + 0.5) * BEAT, hat(q == 3), 0.13, 0.38)
    add(mix, t0 + 2.5 * BEAT, kick(), 0.5)
    for at, d in ((0, 0.85), (1.5 * BEAT, 0.55), (2.5 * BEAT, 0.8)):
        add(mix, t0 + at, bass(root, d), 0.42)
    if b >= 3:                                              # 旋律从 C4（倒水）开始加入
        for k, s in enumerate(MEL[b % 4]):
            if s is not None: add(mix, t0 + k * BEAT / 2, pluck(s + 12), 0.16, 0.3 + 0.4 * (k % 2))

# ---------- 动作音效（都很轻，垫在音乐下面） ----------
sfx = np.zeros((2, N))
for _ in range(9):                                          # C2：豆子落到木桌上
    x = tt(0.04); f = rng.uniform(900, 2200)
    add(sfx, ST[1] + rng.uniform(0.05, 1.1), np.sin(2 * np.pi * f * x) * np.exp(-x * 140), 0.25, rng.uniform(0.3, 0.7))
x = tt(ST[3] - ST[2])                                       # C3：磨豆声
g = lowpass(highpass(rng.standard_normal(len(x)), 300), 2500) * (0.7 + 0.3 * np.sin(2 * np.pi * 23 * x))
add(sfx, ST[2], g / np.abs(g).max() * np.clip(x / 0.05, 0, 1) * np.clip((x[-1] - x) / 0.08, 0, 1), 0.16)
for a, b, lo, hi, v in ((ST[3], ST[4], 1200, 6000, 0.10), (ST[5], ST[6], 500, 3500, 0.13)):   # C4 注水、C6 倒咖啡
    x = tt(b - a); w = lowpass(highpass(rng.standard_normal(len(x)), lo), hi)
    w *= 0.8 + 0.2 * np.sin(2 * np.pi * 7 * x + 3 * np.sin(2 * np.pi * 1.3 * x))
    add(sfx, a, w / np.abs(w).max() * np.clip(x / 0.15, 0, 1) * np.clip((x[-1] - x) / 0.2, 0, 1), v)
for k in range(8):                                          # C5：滴落声，半拍一滴
    x = tt(0.09); f = 700 + 500 * np.exp(-x * 40)
    add(sfx, ST[4] + k * BEAT / 2 + 0.02, np.sin(2 * np.pi * f * x) * np.exp(-x * 45), 0.10, 0.5)

out = mix + sfx
out /= np.abs(out).max() * 1.05
out *= np.clip(t / 0.3, 0, 1) * np.clip((TOTAL - t) / 1.2, 0, 1)
with wave.open("music.wav", "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(SR)
    f.writeframes((np.clip(out, -1, 1).T * 32767).astype("<i2").tobytes())
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", "music.wav", "-t", f"{TOTAL}",
                "-af", "loudnorm=I=-16:TP=-1.5", "-ar", "48000", "-c:a", "aac", "-b:a", "192k", "mix.m4a"], check=True)
print("mix.m4a", TOTAL)
