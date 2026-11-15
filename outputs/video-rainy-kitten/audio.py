"""合成声音：按 timing.py 重排配音 + 代码合成的雨声 + 八音盒配乐 -> mix.m4a
  python3 audio.py src/voice.mp3
雨声：前四镜是室外的雨（亮、沙沙声），S5 换成隔着窗户的雨（闷、小声）。
"""
import subprocess, sys, wave
import numpy as np
from timing import TOTAL, ST, XF, VOICE

SR = 48000
N = int(SR * TOTAL)
t = np.arange(N) / SR
rng = np.random.default_rng(4)

def save(name, x):
    x = np.clip(x, -1, 1)
    with wave.open(name, "wb") as f:
        f.setnchannels(2); f.setsampwidth(2); f.setframerate(SR)
        f.writeframes((x.T * 32767).astype("<i2").tobytes())

def band(noise, lo, hi):
    """用 FFT 做带通：只留 lo~hi Hz。"""
    F = np.fft.rfft(noise); f = np.fft.rfftfreq(len(noise), 1 / SR)
    g = 1 / (1 + (lo / np.maximum(f, 1)) ** 4) / (1 + (f / hi) ** 4)
    return np.fft.irfft(F * g, len(noise))

# ---------- 雨声 ----------
def rain_bed(lo, hi):
    ch = []
    for _ in range(2):
        x = band(rng.standard_normal(N), lo, hi)
        x *= 1 + 0.25 * band(rng.standard_normal(N), 0.05, 0.6) / 0.01   # 忽大忽小的阵雨感
        ch.append(x / np.abs(x).max())
    return np.array(ch)

def drops(count, f_lo, f_hi, amp):
    """零星的雨滴声：很短的衰减正弦。"""
    out = np.zeros((2, N))
    for _ in range(count):
        at = int(rng.uniform(0, TOTAL - 0.2) * SR); f = rng.uniform(f_lo, f_hi)
        n = int(0.06 * SR); tt = np.arange(n) / SR
        w = np.sin(2 * np.pi * f * tt * (1 + 2 * tt)) * np.exp(-tt * 70) * rng.uniform(0.3, 1) * amp
        pan = rng.uniform(0.15, 0.85)
        out[0, at:at + n] += w * (1 - pan); out[1, at:at + n] += w * pan
    return out

outdoor = rain_bed(900, 9000) * 0.55 + rain_bed(150, 900) * 0.25 + drops(260, 1500, 4500, 0.10)
indoor = rain_bed(120, 700) * 0.45 + drops(40, 500, 1100, 0.12)
x5 = ST[4]                                                    # 进入室内的时间
k = np.clip((t - (x5 - XF[4])) / (XF[4] + 0.3), 0, 1)        # 室外 -> 室内
rain = outdoor * (1 - k) * 0.50 + indoor * k * 0.38

# ---------- 八音盒 + 垫底长音 ----------
A = 440.0
def hz(semi): return A * 2 ** (semi / 12)
music = np.zeros((2, N))
def note(at, semi, vol=1.0, pan=0.5, decay=2.2):
    i0 = int(at * SR)
    if i0 >= N: return
    tt = np.arange(N - i0) / SR; f = hz(semi)
    w = (np.sin(2 * np.pi * f * tt) + 0.35 * np.sin(2 * np.pi * 3.01 * f * tt) * np.exp(-tt * 6)
         + 0.2 * np.sin(2 * np.pi * 2 * f * tt)) * np.exp(-tt * decay) * np.clip(tt / 0.004, 0, 1) * vol
    music[0, i0:] += w * (1 - pan); music[1, i0:] += w * pan
# C 大调，前半段稀疏（孤单），遇到小孩之后音多一点，进屋后变成摇篮曲
mel = [(0.8, 7), (2.2, 3), (3.6, 7), (5.0, 10),                       # S1
       (6.4, 7), (7.6, 12), (8.8, 10), (10.0, 7),                      # S2 看见灯
       (11.6, 3), (12.4, 7), (13.2, 10), (14.0, 15), (15.2, 14),       # S3 伞
       (16.6, 12), (17.4, 15), (18.2, 19), (19.0, 15), (19.8, 14), (20.6, 10),   # S4 手
       (21.6, 15), (22.2, 12), (22.8, 15), (23.4, 19), (24.0, 15), (24.6, 12),   # S5 摇篮曲
       (25.2, 10), (25.8, 7), (26.4, 3), (27.0, 3)]
for at, semi in mel:
    note(at, semi, 0.9, 0.35 + 0.3 * rng.random())
for at, semi in [(21.6, -9), (23.4, -14), (25.2, -9), (26.4, -21)]:    # S5 的低音
    note(at, semi, 0.7, 0.5, 1.0)
# 垫底长音：Am（冷） -> F -> C（暖）
for c0, c1, ch in [(0, 11.5, (-12, -5, 0, 3)), (10.5, 21.5, (-16, -9, -4, 0)), (20.5, TOTAL, (-21, -9, -2, 3, 7))]:
    env = np.clip((t - c0) / 2.0, 0, 1) * np.clip((c1 - t) / 2.0, 0, 1)
    for semi in ch:
        for det, pan in ((-0.3, 0.3), (0.3, 0.7)):
            w = np.sin(2 * np.pi * (hz(semi) + det) * t + rng.uniform(0, 6.28)) * env * 0.16
            music[0] += w * (1 - pan); music[1] += w * pan
music /= np.abs(music).max()

fade = np.clip(t / 1.0, 0, 1) * np.clip((TOTAL - t) / 2.0, 0, 1)
save("rain.wav", rain * fade)
save("music.wav", music * fade * 0.9)

# ---------- 配音重排 + 混音 ----------
voice = sys.argv[1]
parts = "".join(f"[0:a]atrim={a:.3f}:{b:.3f},asetpts=PTS-STARTPTS,afade=t=in:d=0.04,afade=t=out:st={b - a - 0.06:.3f}:d=0.06,"
                f"adelay={int(at * 1000)}:all=1[v{i}];" for i, (a, b, at) in enumerate(VOICE))
fc = (parts + "".join(f"[v{i}]" for i in range(len(VOICE))) +
      f"amix=inputs={len(VOICE)}:normalize=0,apad=whole_dur={TOTAL},loudnorm=I=-15:TP=-1.5,asplit[vo][sc];"
      "[1:a]volume=0.3[rn];[2:a]volume=0.42[mu];[rn][mu]amix=inputs=2:normalize=0[bed];"
      "[bed][sc]sidechaincompress=threshold=0.03:ratio=4:attack=30:release=500[duck];"   # 有人声时把雨声和音乐压低
      "[vo][duck]amix=inputs=2:normalize=0,alimiter=limit=0.9[out]")
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", voice, "-i", "rain.wav", "-i", "music.wav",
                "-filter_complex", fc, "-map", "[out]", "-t", f"{TOTAL}", "-c:a", "aac", "-b:a", "160k", "mix.m4a"], check=True)
print("mix.m4a", TOTAL)
