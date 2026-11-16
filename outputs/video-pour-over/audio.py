"""练习 5 v2 舒缓版的声音：代码合成的慢速配乐（没有鼓）+ 很轻的动作音效 -> mix.m4a
  python3 audio.py
长音铺底 + 电钢琴慢分解和弦 + 稀疏的旋律，按 timing.py 的拍子排（一拍 0.9 秒）。
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
    if i0 >= N or i0 < 0: return
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

def epiano(semi, d=3.0):                                  # 柔和的电钢琴：起音慢一点，衰减长
    x = tt(d); f = hz(semi)
    return (np.sin(2 * np.pi * f * x + 0.8 * np.sin(2 * np.pi * f * x) * np.exp(-x * 4))
            * np.exp(-x * 1.3) * np.clip(x / 0.02, 0, 1) * np.clip((d - x) / 0.3, 0, 1))
def bell(semi, d=3.5):                                    # 旋律用的钟琴音色
    x = tt(d); f = hz(semi)
    return (np.sin(2 * np.pi * f * x) + 0.25 * np.sin(2 * np.pi * 2 * f * x) * np.exp(-x * 5)) * np.exp(-x * 1.6) * np.clip(x / 0.01, 0, 1)

BAR = 4 * BEAT
BARS = int(round(TOTAL / BAR))                            # 8 小节，每小节 3.6 秒
# 和弦：Cmaj7 - Am7 - Fmaj7 - G6（每小节一个，循环两遍）
CH = [[-12, 0, 4, 7, 11], [-15, -3, 0, 4, 7], [-19, -7, -3, 0, 4], [-17, -5, -1, 2, 4]]
for b in range(BARS):
    t0 = b * BAR
    ch = CH[b % 4]
    # 铺底长音：前后小节互相交叠，听不出换和弦的"接缝"
    env = np.clip((t - (t0 - 0.9)) / 1.4, 0, 1) * np.clip((t0 + BAR + 0.9 - t) / 1.4, 0, 1)
    for s in ch:
        for det, pan in ((-0.35, 0.3), (0.35, 0.7)):
            w = np.sin(2 * np.pi * (hz(s) + det) * t + rng.uniform(0, 6.28)) * env * (0.05 if s > 3 else 0.07)
            mix[0] += w * (1 - pan); mix[1] += w * pan
    # 分解和弦：每半拍一个音，上行再下行；最后一小节只弹一遍上行然后停住
    order = [1, 2, 3, 4, 3, 2, 3, 4] if b < BARS - 1 else [1, 2, 3, 4]
    for k, idx in enumerate(order):
        add(mix, t0 + k * BEAT / 2, epiano(ch[idx]), 0.10 if k % 2 else 0.13, 0.4 + 0.05 * idx)
    add(mix, t0, epiano(ch[0], 3.4), 0.16)                # 低音
# 旋律：从第 3 小节（研磨之后）进来，音很少
MEL = [(2, 0, 16), (2, 2, 14), (3, 0, 12), (3, 2.5, 9), (4, 0, 11), (4, 2, 12), (5, 0, 16), (5, 2, 19),
       (6, 0, 17), (6, 2, 16), (7, 0, 14), (7, 1.5, 12)]
for b, q, s in MEL:
    add(mix, b * BAR + q * BEAT, bell(s), 0.12, 0.55)

# ---------- 动作音效（比 v1 轻一半，只是一点质感） ----------
sfx = np.zeros((2, N))
for _ in range(6):                                          # C2：豆子落到木桌上
    x = tt(0.05); f = rng.uniform(700, 1500)
    add(sfx, ST[1] + rng.uniform(0.6, 2.4), np.sin(2 * np.pi * f * x) * np.exp(-x * 110), 0.09, rng.uniform(0.3, 0.7))
for a, b, lo, hi, v in ((ST[2], ST[3], 300, 2000, 0.045),   # C3 研磨
                        (ST[3], ST[4], 900, 5000, 0.05),    # C4 注水
                        (ST[5], ST[6], 400, 3000, 0.06)):   # C6 倒咖啡
    x = tt(b - a); w = lowpass(highpass(rng.standard_normal(len(x)), lo), hi)
    w *= 0.85 + 0.15 * np.sin(2 * np.pi * 5 * x + 3 * np.sin(2 * np.pi * 1.1 * x))
    add(sfx, a, w / np.abs(w).max() * np.clip((x - 0.3) / 0.8, 0, 1) * np.clip((x[-1] - x) / 0.8, 0, 1), v)
for k in range(4):                                          # C5：滴落声，一拍一滴
    x = tt(0.12); f = 600 + 400 * np.exp(-x * 35)
    add(sfx, ST[4] + (k + 0.5) * BEAT, np.sin(2 * np.pi * f * x) * np.exp(-x * 35), 0.05, 0.5)

out = mix + sfx
out /= np.abs(out).max() * 1.1
out *= np.clip(t / 1.5, 0, 1) * np.clip((TOTAL - t) / 2.5, 0, 1)
with wave.open("music.wav", "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(SR)
    f.writeframes((np.clip(out, -1, 1).T * 32767).astype("<i2").tobytes())
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", "music.wav", "-t", f"{TOTAL}",
                "-af", "loudnorm=I=-18:TP=-2", "-ar", "48000", "-c:a", "aac", "-b:a", "192k", "mix.m4a"], check=True)
print("mix.m4a", TOTAL)
