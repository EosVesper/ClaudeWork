"""晚霞短片 v2：动态镜头 + 大气层示意动画 + 字幕 + 片尾。
  python3 render.py preview t1 t2 ...   -> contact.png
  python3 render.py render              -> video.mp4（无声）
"""
import json, math, os, random, subprocess, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, FPS, XF = 1080, 1920, 30, 0.4
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
PRISM = "/mnt/user-data/uploads/fd185b0c-image.png"

tm = json.load(open("/home/claude/sunset/timing.json"))
sd = tm["scene_dur"]
DUR = [sd[0], sd[1], sd[2], sd[3] + sd[4], sd[5], sd[6], tm["end_dur"]]
KIND = ["c01", "prism", "c03", "diagram", "c06", "c07", "end"]
ST = [0.0]
for d in DUR: ST.append(ST[-1] + d)
TOTAL = ST[-1]
CAPS = tm["captions"]
CAP_Y = 1330

def font(s, _c={}):
    if s not in _c: _c[s] = ImageFont.truetype(FONT, int(s), index=2)
    return _c[s]
def clamp(x): return max(0.0, min(1.0, x))
def ease(x):
    x = clamp(x); return x * x * (3 - 2 * x)
def lerp(a, b, k): return a + (b - a) * k

# ---------- 视频片段 ----------
_clip = {}
def clip_frame(name, u):
    n = len([f for f in os.listdir(os.path.join(HERE, "fr")) if f.startswith(name)])
    i = min(n, max(1, int(u * FPS) + 1))
    key = (name, i)
    if key not in _clip:
        if len(_clip) > 8: _clip.clear()
        _clip[key] = Image.open(os.path.join(HERE, "fr", f"{name}_{i:03d}.jpg")).convert("RGB")
    return _clip[key]

# ---------- 棱镜静态图（慢拉远） ----------
_pr = None
def prism_frame(u, span):
    global _pr
    if _pr is None: _pr = Image.open(PRISM).convert("RGB")
    w, h = _pr.size
    a = 1 - clamp(u / span)
    z = 1 + 0.08 * a
    S = max(W / w, H / h) * z
    ww, wh = W / S, H / S
    cx, cy = w / 2 + 0.05 * w * a, h / 2 + 0.05 * h * a
    return _pr.transform((W, H), Image.AFFINE, (1 / S, 0, cx - ww / 2, 0, 1 / S, cy - wh / 2),
                         resample=Image.BICUBIC)

# ---------- 大气层示意图 ----------
EC, ER, AT = (540, 2080), 900, 120       # 地心、地球半径、大气层厚度（示意，已夸大）
OBS = (540, EC[1] - ER)                  # 观察者 (540,1180)
RAY_Y = OBS[1] - 6
ENTER_X = 540 - math.sqrt((ER + AT) ** 2 - ER ** 2)   # 傍晚光线进入大气层的位置 ≈ 60
_bg = None
def diagram_bg():
    global _bg
    if _bg is None:
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot(xx - EC[0], yy - EC[1])
        img = np.zeros((H, W, 3), np.float32)
        img[:] = (6, 9, 18)
        k = np.clip((r - ER) / AT, 0, 1)
        band = ((r >= ER) & (r <= ER + AT)).astype(np.float32) * (1 - k) ** 0.8
        halo = np.clip(1 - (r - ER - AT) / 60, 0, 1) * (r > ER + AT) * 0.10
        for c, v in enumerate((60, 140, 255)):
            img[..., c] += v * (band * 0.9 + halo)
        earth = r < ER
        shade = np.clip(1 - (ER - r) / 500, 0, 1)
        for c, v in enumerate((14, 30, 60)):
            img[..., c] = np.where(earth, 8 + v * shade, img[..., c])
        _bg = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
        d = ImageDraw.Draw(_bg)
        rnd = random.Random(7)
        for _ in range(140):
            x, y = rnd.uniform(0, W), rnd.uniform(0, 1000)
            if math.hypot(x - EC[0], y - EC[1]) > ER + AT + 40:
                s = rnd.choice((1, 1, 2)); v = rnd.randint(90, 200)
                d.ellipse((x, y, x + s, y + s), fill=(v, v, v))
    return _bg

RND = random.Random(3)
PART = [(RND.uniform(17.0, 19.4), RND.uniform(ENTER_X + 30, 520), RND.uniform(-2.6, -0.5) if RND.random() < 0.75 else RND.uniform(0.5, 2.6),
         RND.uniform(90, 200), RND.uniform(7, 13)) for _ in range(130)]

def ray_col(x, g):
    """光线在 x 处的颜色；g=0 全白，g=1 完全显示出一路变红。"""
    p = clamp((x - ENTER_X) / (540 - ENTER_X)) * g
    stops = [(255, 255, 245), (255, 225, 140), (255, 150, 60), (255, 80, 40)]
    f = p * 3; i = min(2, int(f)); k = f - i
    return tuple(int(lerp(stops[i][c], stops[i + 1][c], k)) for c in range(3))

def diagram_frame(t):
    img = diagram_bg().convert("RGBA")
    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    t0 = ST[3]
    # 正午：太阳在头顶，光线竖直穿过薄薄一层
    a = ease((t - t0 - 0.1) / 0.4) * lerp(1, 0.3, ease((t - 14.3) / 0.5))
    A = int(255 * a)
    if A > 0:
        d.ellipse((540 - 50, 300 - 50, 540 + 50, 300 + 50), fill=(255, 240, 200, A))
        y1 = lerp(360, OBS[1], ease((t - t0 - 0.2) / 0.6))
        d.line((540, 360, 540, y1), fill=(255, 250, 230, A), width=14)
        d.text((610, 290), "正午", font=font(60), fill=(255, 255, 255, A), anchor="lm")
        d.text((610, 362), "穿过的大气很薄", font=font(44), fill=(170, 180, 195, A), anchor="lm")
    # 傍晚：太阳在地平线，光线贴着地面斜穿过大气
    b = ease((t - 14.3) / 0.5)
    B = int(255 * b)
    if B > 0:
        d.ellipse((-60, RAY_Y - 60, 60, RAY_Y + 60), fill=(255, 200, 120, B))
        head = lerp(40, 540, ease((t - 14.6) / 1.8))
        g = ease((t - 16.6) / 1.8)
        x = 40
        while x < head:
            x2 = min(head, x + 8)
            d.line((x, RAY_Y, x2, RAY_Y), fill=ray_col(x, g) + (B,), width=18)
            x = x2
        la = int(255 * ease((t - 15.3) / 0.4))
        d.text((70, 985), "傍晚", font=font(60), fill=(255, 255, 255, la), anchor="lm")
        d.text((70, 1057), "斜着穿过，路程长得多", font=font(44), fill=(170, 180, 195, la), anchor="lm")
        # 被散掉的蓝光
        for tb, px, dirv, sp, r in PART:
            life = (t - tb) / 1.0
            if 0 < life < 1:
                ang = math.atan2(dirv, 0.6)
                x = px + math.cos(ang) * sp * life * 0.5
                y = RAY_Y + math.sin(ang) * sp * life
                al = int(230 * (1 - life) ** 1.2)
                d.ellipse((x - r, y - r, x + r, y + r), fill=(90, 160, 255, al))
        # 到达观察者的光：只剩红橙
        ga = int(200 * g)
        if ga > 0:
            for rr, al in ((70, 0.18), (44, 0.35), (24, 0.7)):
                d.ellipse((OBS[0] - rr, RAY_Y - rr, OBS[0] + rr, RAY_Y + rr), fill=(255, 100, 40, int(ga * al)))
    # 观察者、大气层标注
    c = int(255 * ease((t - t0) / 0.4))
    d.ellipse((OBS[0] - 9, OBS[1] - 9, OBS[0] + 9, OBS[1] + 9), fill=(255, 255, 255, c))
    d.text((OBS[0] + 26, OBS[1] + 36), "你", font=font(46), fill=(255, 255, 255, c), anchor="lm")
    d.text((1040, 1215), "大气层", font=font(42), fill=(150, 195, 255, c), anchor="rm")
    glow = ov.filter(ImageFilter.GaussianBlur(10))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, ov)
    return img.convert("RGB")

# ---------- 片尾 ----------
_end = None
def end_frame(u):
    global _end
    if _end is None:
        _end = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(_end)
        d.text((W / 2, 880), "晚霞", font=font(150), fill=(255, 140, 70, 255), anchor="mm")
        d.text((W / 2, 1040), "是空气筛出来的颜色", font=font(64), fill=(255, 255, 255, 235), anchor="mm")
    base = Image.new("RGB", (W, H), (0, 0, 0))
    k = clamp((u - XF) / 0.6)
    if k > 0:
        layer = _end.copy(); layer.putalpha(layer.getchannel("A").point(lambda v: int(v * k)))
        base.paste(layer, (0, 0), layer)
    return base

def scene_frame(i, T):
    u = T - ST[i] + XF          # 每个场景提前 XF 秒开始（用于叠化）
    k = KIND[i]
    if k == "prism": return prism_frame(u, DUR[i] + XF)
    if k == "diagram": return diagram_frame(T)
    if k == "end": return end_frame(u)
    return clip_frame(k, u)

_cap = {}
def cap_img(text):
    if text not in _cap:
        layer = Image.new("RGBA", (W, 200), (0, 0, 0, 0))
        ImageDraw.Draw(layer).text((W / 2, 100), text, font=font(66), fill=(0, 0, 0, 170), anchor="mm")
        sh = layer.filter(ImageFilter.GaussianBlur(10))
        ImageDraw.Draw(sh).text((W / 2, 100), text, font=font(66), fill=(255, 255, 255, 255), anchor="mm",
                                stroke_width=4, stroke_fill=(15, 15, 15, 235))
        _cap[text] = sh
    return _cap[text]

def frame_at(T):
    i = min(len(DUR) - 1, max(k for k in range(len(DUR)) if ST[k] <= T + 1e-9))
    img = scene_frame(i, T)
    tail = ST[i + 1] - T
    if i + 1 < len(DUR) and tail < XF:
        img = Image.blend(img, scene_frame(i + 1, T), 1 - tail / XF)
    else:
        img = img.copy()
    for s, e, text in CAPS:
        if s <= T < e:
            k = min(1.0, (T - s) / 0.12, (e - T) / 0.12)
            c = cap_img(text)
            if k < 1:
                c = c.copy(); c.putalpha(c.getchannel("A").point(lambda v: int(v * k)))
            img.paste(c, (0, CAP_Y - 100), c)
    return img

def render_scene(i):
    out = os.path.join(HERE, "parts", f"s{i:02d}.mp4"); tmp = out[:-4] + ".tmp.mp4"
    p = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                          "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-crf", "18",
                          "-preset", "veryfast", "-pix_fmt", "yuv420p", "-r", str(FPS), tmp], stdin=subprocess.PIPE)
    for n in range(round(ST[i] * FPS), round(ST[i + 1] * FPS)):
        p.stdin.write(frame_at(n / FPS).tobytes())
    p.stdin.close()
    if p.wait() != 0: raise SystemExit(f"scene {i} failed")
    os.replace(tmp, out)
    return out

if __name__ == "__main__":
    if sys.argv[1] == "preview":
        ts = [float(x) for x in sys.argv[2:]]
        sheet = Image.new("RGB", (len(ts) * 270, 480))
        for k, t in enumerate(ts):
            sheet.paste(frame_at(t).resize((270, 480), Image.LANCZOS), (k * 270, 0))
        sheet.save(os.path.join(HERE, "contact.png"))
    else:
        scenes = [int(x) for x in sys.argv[2:]] or list(range(len(DUR)))
        os.makedirs(os.path.join(HERE, "parts"), exist_ok=True)
        with Pool(2) as pool:
            pool.map(render_scene, scenes, chunksize=1)
        lst = os.path.join(HERE, "parts", "list.txt")
        open(lst, "w").write("".join(f"file '{HERE}/parts/s{i:02d}.mp4'\n" for i in range(len(DUR))))
        tmp = os.path.join(HERE, "video.tmp.mp4")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst,
                        "-c", "copy", tmp], check=True)
        os.replace(tmp, os.path.join(HERE, "video.mp4"))
        print("done", round(TOTAL, 2), ST)
