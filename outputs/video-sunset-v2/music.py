"""用代码合成一段氛围铺底音乐（长音和弦 + 稀疏的泛音点缀）。"""
import numpy as np, wave, sys
SR, DUR = 48000, float(sys.argv[1])
t = np.arange(int(SR * DUR)) / SR
out = np.zeros((2, len(t)))
A = 220.0
def n(semi): return A * 2 ** (semi / 12)
# Am9 - Fmaj7 - C - G（每个约 7.4 秒，交叠渐变）
chords = [[-12, 0, 3, 7, 14], [-16, -4, 0, 5, 12], [-21, -9, 3, 7, 10], [-14, -2, 2, 5, 14]]
seg = DUR / len(chords)
rng = np.random.default_rng(1)
for k, ch in enumerate(chords):
    c = (k + 0.5) * seg
    env = np.clip(1 - np.abs(t - c) / (seg * 0.75), 0, 1) ** 1.5
    for semi in ch:
        f = n(semi)
        for det, pan in ((-0.25, 0.3), (0.25, 0.7), (0, 0.5)):
            ph = rng.uniform(0, 6.28)
            w = np.sin(2 * np.pi * (f + det) * t + ph) + 0.25 * np.sin(2 * np.pi * 2 * (f + det) * t + ph)
            w *= env * (0.5 if semi > 6 else 1.0) * (1 + 0.15 * np.sin(2 * np.pi * 0.13 * t + ph))
            out[0] += w * (1 - pan); out[1] += w * pan
# 泛音点缀
for tb, semi, pan in [(2.5, 24, .3), (6.0, 19, .7), (9.5, 26, .4), (13.5, 17, .6), (17.0, 24, .3), (20.5, 22, .7), (23.5, 26, .5), (26.5, 31, .4)]:
    if tb < DUR:
        tt = np.clip(t - tb, 0, None)
        w = np.sin(2 * np.pi * n(semi) * tt) * np.exp(-tt * 1.3) * (t >= tb) * np.clip(tt / 0.02, 0, 1) * 2.2
        out[0] += w * (1 - pan); out[1] += w * pan
fade = np.clip(t / 1.5, 0, 1) * np.clip((DUR - t) / 2.5, 0, 1)
out *= fade
out /= np.abs(out).max() * 1.15
with wave.open("music.wav", "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(SR)
    f.writeframes((out.T * 32767).astype("<i2").tobytes())
