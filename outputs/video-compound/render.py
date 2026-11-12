"""《每天进步 1%》纯代码动画。用法：
  python3 render.py preview t1 t2 ...   # 关键帧拼成 contact.png
  python3 render.py render              # 分场景渲染并拼接
"""
import os, subprocess, sys
from multiprocessing import Pool
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, FPS, S = 1080, 1920, 30, 2
XF = 0.3
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
BG, WHITE, GRAY, DIM = (13, 17, 23), (240, 244, 248), (125, 135, 150), (38, 46, 58)
ORANGE, BLUE = (255, 140, 70), (90, 169, 255)

BOUNDS = [0, 4.5, 11.6, 15.6, 26.2, 32.4, 38.6]
TOTAL = BOUNDS[-1]
CAPS = [(0.39, 2.02, "每天进步 1%"), (2.41, 4.17, "一年后你会变成多少？"),
        (4.81, 6.34, "如果只是简单相加"), (6.52, 7.80, "是 4.65 倍"),
        (8.49, 9.52, "但按复利算"), (9.86, 11.19, "是 37.8 倍"),
        (12.05, 13.38, "因为每天的进步"), (13.51, 15.20, "都建立在昨天的基础上"),
        (16.01, 18.32, "前一百天，曲线几乎是平的"), (18.83, 20.33, "你会觉得什么都没变"),
        (21.21, 23.30, "过了两百天，它开始抬头"), (23.99, 25.88, "到了年底，直线起飞"),
        (26.54, 29.13, "反过来，每天退步 1%"), (29.77, 31.92, "一年后只剩不到 3%")]
CAP_Y = 1480

_f = {}
def font(size):
    if size not in _f:
        _f[size] = ImageFont.truetype(FONT, int(size), index=2)
    return _f[size]

def clamp(x): return max(0.0, min(1.0, x))
def ease(x):
    x = clamp(x); return x * x * (3 - 2 * x)
def ap(t, t0, dur=0.35): return ease((t - t0) / dur)
def mix(a, b, k): return tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))
def lerp(a, b, k): return a + (b - a) * k

def T(d, x, y, s, size, col, a=1.0, anchor="mm"):
    if a <= 0: return
    d.text((x * S, (y + (1 - a) * 18) * S), s, font=font(size * S), fill=mix(BG, col, a), anchor=anchor)

def R(d, x0, y0, x1, y1, col, r=0):
    if x1 <= x0 or y1 <= y0: return
    d.rounded_rectangle((x0 * S, y0 * S, x1 * S, y1 * S), radius=r * S, fill=col)

def L(d, pts, col, w):
    d.line([(x * S, y * S) for x, y in pts], fill=col, width=int(w * S), joint="curve")

def DOT(d, x, y, r, col):
    d.ellipse(((x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S), fill=col)

# ---------- 场景 A：钩子 ----------
def scene_a(d, t):
    T(d, 540, 320, "每天进步", 110, WHITE, ap(t, 0.25))
    T(d, 540, 520, "1%", 250, ORANGE, ap(t, 0.45))
    n = int(lerp(1, 365, ease((t - 0.8) / 2.6)))
    for k in range(365):
        x, y = 132 + (k % 25) * 34, 760 + (k // 25) * 34
        DOT(d, x, y, 10, ORANGE if k < n else DIM)
    a = ap(t, 0.8)
    T(d, 540, 1330, f"第 {n} 天", 72, WHITE, a)
    q = ap(t, 2.9)
    if q > 0:
        T(d, 540, 1330, f"第 {n} 天", 72, BG, 1)          # 擦掉居中的，改成带问号的一行
        T(d, 470, 1330, f"第 {n} 天  →", 72, WHITE, 1)
        T(d, 760, 1326, "?", 110, ORANGE, q)

# ---------- 场景 B：相加 vs 复利 ----------
def scene_b(d, t):
    T(d, 540, 250, "365 天后", 72, GRAY, ap(t, 4.6))
    base = 1250
    L(d, [(120, base), (960, base)], DIM, 4)
    # 左：简单相加
    a = ap(t, 4.8)
    k = ease((t - 5.0) / 1.9)
    v = lerp(1, 4.65, k)
    h = v * 800 / 37.8
    R(d, 190, base - h, 410, base, mix(BG, BLUE, a), 10)
    T(d, 300, base - h - 70, f"{v:.2f} 倍", 84, BLUE, a)
    T(d, 300, 1310, "简单相加", 56, WHITE, a)
    T(d, 300, 1378, "1 + 0.01 × 365", 42, GRAY, a)
    # 右：复利
    a = ap(t, 8.45)
    k = ease((t - 9.5) / 1.15)
    v = lerp(1, 37.8, k)
    h = v * 800 / 37.8
    R(d, 670, base - h, 890, base, mix(BG, ORANGE, a), 10)
    T(d, 780, base - h - 80, f"{v:.1f} 倍", 110, ORANGE, a)
    T(d, 780, 1310, "复利", 56, WHITE, a)
    T(d, 780, 1378, "1.01 连乘 365 次", 42, GRAY, a)

# ---------- 场景 C：建立在昨天的基础上 ----------
def scene_c(d, t):
    T(d, 540, 300, "每一天，都在昨天上 × 1.01", 60, GRAY, ap(t, 11.7))
    rows = [(12.0, "第 1 天", "1.0000"), (12.7, "第 2 天", "1.0100"),
            (13.4, "第 3 天", "1.0201"), (14.1, "第 4 天", "1.0303")]
    for i, (t0, lab, val) in enumerate(rows):
        y, a = 480 + i * 170, ap(t, t0)
        R(d, 170, y - 55, 910, y + 55, mix(BG, (24, 30, 40), a), 22)
        T(d, 230, y, lab, 56, GRAY, a, "lm")
        T(d, 850, y, val, 72, WHITE, a, "rm")
        if i:
            T(d, 540, y - 85, "↓ × 1.01", 38, ORANGE, a)
    a = ap(t, 14.7)
    for dy in (-22, 0, 22):
        DOT(d, 540, 1105 + dy, 6, mix(BG, GRAY, a))
    y = 1240
    R(d, 170, y - 65, 910, y + 65, mix(BG, (60, 36, 22), a), 22)
    T(d, 230, y, "第 365 天", 56, ORANGE, a, "lm")
    T(d, 850, y, "37.78", 90, ORANGE, a, "rm")

# ---------- 图表公共部分 ----------
PX0, PX1, PY0, PY1 = 150, 960, 1300, 520
def px(day): return PX0 + (PX1 - PX0) * day / 365
def frame(d, a, ylabels, ymax):
    for v, lab in ylabels:
        y = PY0 - (PY0 - PY1) * v / ymax
        L(d, [(PX0, y), (PX1, y)], mix(BG, DIM, a), 2)
        T(d, PX0 - 14, y, lab, 34, GRAY, a, "rm")
    L(d, [(PX0, PY0), (PX1, PY0)], mix(BG, GRAY, a), 3)
    for day in (100, 200, 365):
        T(d, px(day), PY0 + 40, f"第{day}天", 34, GRAY, a)

def keyed(t, keys):
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t <= t1:
            return lerp(v0, v1, ease((t - t0) / (t1 - t0)))
    return keys[-1][1]

# ---------- 场景 D：复利曲线 ----------
def scene_d(d, t):
    a = ap(t, 15.7)
    frame(d, a, [(10, "10倍"), (20, "20倍"), (30, "30倍"), (40, "40倍")], 40)
    py = lambda v: PY0 - (PY0 - PY1) * v / 40
    day = keyed(t, [(16.0, 0), (18.2, 100), (21.2, 100), (23.2, 200), (24.0, 200), (25.8, 365)])
    val = 1.01 ** day
    T(d, 540, 270, f"第 {int(round(day))} 天", 64, GRAY, a)
    T(d, 540, 390, f"{val:.1f} 倍", 150, ORANGE, a)
    days = [i for i in range(0, int(day) + 1)] + [day]
    pts = [(px(x), py(1.01 ** x)) for x in days]
    if len(pts) > 1:
        poly = pts + [(pts[-1][0], PY0), (PX0, PY0)]
        d.polygon([(x * S, y * S) for x, y in poly], fill=mix(BG, ORANGE, 0.16))
        L(d, pts, ORANGE, 9)
    for m, lab, dy in ((100, "2.7 倍", -52), (200, "7.3 倍", -52), (365, "37.8 倍", 0)):
        if day >= m - 0.01:
            k = ap(t, [18.2, 23.2, 25.8][(100, 200, 365).index(m)], 0.3)
            DOT(d, px(m), py(1.01 ** m), 13, WHITE)
            DOT(d, px(m), py(1.01 ** m), 8, ORANGE)
            if dy:
                T(d, px(m), py(1.01 ** m) + dy, lab, 44, WHITE, k)
    hx, hy = pts[-1]
    DOT(d, hx, hy, 15, mix(BG, WHITE, a)); DOT(d, hx, hy, 9, mix(BG, ORANGE, a))

# ---------- 场景 E：退步曲线 ----------
def scene_e(d, t):
    a = ap(t, 26.3)
    frame(d, a, [(25, "25%"), (50, "50%"), (75, "75%"), (100, "100%")], 100)
    py = lambda v: PY0 - (PY0 - PY1) * v / 100
    day = keyed(t, [(27.6, 0), (31.6, 365)])
    val = 100 * 0.99 ** day
    T(d, 540, 270, f"每天退步 1% · 第 {int(round(day))} 天", 56, GRAY, a)
    T(d, 540, 390, f"剩 {val:.1f}%", 150, BLUE, a)
    days = [i for i in range(0, int(day) + 1)] + [day]
    pts = [(px(x), py(100 * 0.99 ** x)) for x in days]
    if len(pts) > 1:
        poly = pts + [(pts[-1][0], PY0), (PX0, PY0)]
        d.polygon([(x * S, y * S) for x, y in poly], fill=mix(BG, BLUE, 0.16))
        L(d, pts, BLUE, 9)
    hx, hy = pts[-1]
    DOT(d, hx, hy, 15, mix(BG, WHITE, a)); DOT(d, hx, hy, 9, mix(BG, BLUE, a))
    T(d, PX1 - 10, PY0 - 90, "不到 3%", 52, WHITE, ap(t, 31.6, 0.3), "rm")

# ---------- 场景 F：收尾 ----------
def scene_f(d, t):
    a = ap(t, 32.5)
    T(d, 150, 400, "每天 +1%", 56, GRAY, a, "lm")
    T(d, 930, 400, "37.8 倍", 120, ORANGE, a, "rm")
    L(d, [(150, 500), (930, 500)], mix(BG, DIM, a), 3)
    T(d, 150, 600, "每天 −1%", 56, GRAY, a, "lm")
    T(d, 930, 600, "0.03 倍", 120, BLUE, a, "rm")
    T(d, 540, 960, "差别不在某一天", 96, WHITE, ap(t, 32.8))
    T(d, 540, 1140, "而在每一天", 150, ORANGE, ap(t, 34.55))

SCENES = [scene_a, scene_b, scene_c, scene_d, scene_e, scene_f]

def draw_scene(i, t):
    im = Image.new("RGB", (W * S, H * S), BG)
    SCENES[i](ImageDraw.Draw(im), t)
    return im.reduce(S)

_cap = {}
def cap_img(text):
    if text not in _cap:
        layer = Image.new("RGBA", (W, 160), (0, 0, 0, 0))
        ImageDraw.Draw(layer).text((W / 2, 80), text, font=font(58), fill=(255, 255, 255, 255),
                                   anchor="mm", stroke_width=4, stroke_fill=(0, 0, 0, 255))
        _cap[text] = layer
    return _cap[text]

def frame_at(t):
    i = max(k for k in range(len(SCENES)) if BOUNDS[k] <= t + 1e-9)
    img = draw_scene(i, t)
    tail = BOUNDS[i + 1] - t
    if i + 1 < len(SCENES) and tail < XF:
        img = Image.blend(img, draw_scene(i + 1, t), 1 - tail / XF)
    for k, (s, e, text) in enumerate(CAPS):
        e2 = min(e + 0.45, CAPS[k + 1][0] - 0.03) if k + 1 < len(CAPS) else e + 0.3
        if s - 0.05 <= t < e2:
            a = min(1.0, (t - s + 0.05) / 0.12, (e2 - t) / 0.12)
            c = cap_img(text)
            if a < 1:
                c = c.copy(); c.putalpha(c.getchannel("A").point(lambda v: int(v * a)))
            img.paste(c, (0, CAP_Y - 80), c)
    if t > TOTAL - 0.4:
        img = Image.blend(img, Image.new("RGB", (W, H), (0, 0, 0)), clamp((t - TOTAL + 0.4) / 0.4))
    return img

def render_scene(i):
    out = os.path.join(HERE, "parts", f"s{i:02d}.mp4")
    tmp = out[:-4] + ".tmp.mp4"
    p = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                          "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-crf", "18",
                          "-preset", "veryfast", "-pix_fmt", "yuv420p", "-r", str(FPS), tmp],
                         stdin=subprocess.PIPE)
    for n in range(round(BOUNDS[i] * FPS), round(BOUNDS[i + 1] * FPS)):
        p.stdin.write(frame_at(n / FPS).tobytes())
    p.stdin.close()
    if p.wait() != 0:
        raise SystemExit(f"scene {i} failed")
    os.replace(tmp, out)
    return out

if __name__ == "__main__":
    if sys.argv[1] == "preview":
        ts = [float(x) for x in sys.argv[2:]]
        sheet = Image.new("RGB", (len(ts) * 270, 480))
        for k, t in enumerate(ts):
            sheet.paste(frame_at(t).resize((270, 480), Image.LANCZOS), (k * 270, 0))
        sheet.save(os.path.join(HERE, "contact.png"))
        frame_at(ts[-1]).save(os.path.join(HERE, "full.png"))
    else:
        os.makedirs(os.path.join(HERE, "parts"), exist_ok=True)
        with Pool(2) as pool:
            names = pool.map(render_scene, range(len(SCENES)), chunksize=1)
        lst = os.path.join(HERE, "parts", "list.txt")
        open(lst, "w").write("".join(f"file '{n}'\n" for n in names))
        tmp = os.path.join(HERE, "video.tmp.mp4")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst,
                        "-c", "copy", tmp], check=True)
        os.replace(tmp, os.path.join(HERE, "video.mp4"))
        print("done", TOTAL)
