"""练习 5《一杯手冲咖啡》：8 个镜头卡点快剪 + 弹出文字 + 片尾。
先运行 sh prep.sh 抽帧到 fr2/。
  python3 render.py preview t1 t2 ...   -> contact.png
  python3 render.py render [场景号...]  -> video.mp4（无声）
"""
import glob, os, subprocess, sys
from multiprocessing import Pool
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from timing import SHOTS, DUR, ST, TOTAL, BEAT, END_AT

W, H, FPS = 1080, 1920, 30
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
SERIF = "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc"
TEXT_Y = 1430
ACCENT = (255, 196, 120)

def font(s, path=FONT, _c={}):
    if (s, path) not in _c: _c[(s, path)] = ImageFont.truetype(path, int(s), index=2)
    return _c[(s, path)]
def clamp(x): return max(0.0, min(1.0, x))
def ease(x):
    x = clamp(x); return x * x * (3 - 2 * x)

_n = {}
def nframes(name):
    if name not in _n: _n[name] = len(glob.glob(os.path.join(HERE, "fr2", f"{name}_*.jpg")))
    return _n[name]

def scene_frame(i, T):
    name = SHOTS[i][0]
    u = T - ST[i]
    k = min(nframes(name), max(1, int(round(u * FPS)) + 1))      # 帧用完后停在最后一帧（C8 的定格）
    src = Image.open(os.path.join(HERE, "fr2", f"{name}_{k:03d}.jpg")).convert("RGB")
    # 运镜：切入瞬间"顿"一下（放大 6% 后 0.25 秒内弹回），之后整镜慢推 4%
    z = 1 + 0.06 * (1 - ease(u / 0.25)) + 0.04 * (u / DUR[i])
    w, h = src.size
    S = W / w * z
    ww, wh = W / S, H / S
    return src.transform((W, H), Image.AFFINE, (1 / S, 0, (w - ww) / 2, 0, 1 / S, (h - wh) / 2),
                         resample=Image.BICUBIC)

_txt = {}
def text_layer(big, small):
    """大字 + 小字，带柔和阴影，画在一张透明图上（中心在图的中心）。"""
    if (big, small) not in _txt:
        L = Image.new("RGBA", (W, 420), (0, 0, 0, 0))
        d = ImageDraw.Draw(L)
        d.text((W / 2, 150), big, font=font(150), fill=(0, 0, 0, 200), anchor="mm")
        if small: d.text((W / 2, 300), small, font=font(58), fill=(0, 0, 0, 200), anchor="mm")
        L = L.filter(ImageFilter.GaussianBlur(14))
        d = ImageDraw.Draw(L)
        d.text((W / 2, 150), big, font=font(150), fill=(255, 255, 255, 255), anchor="mm",
               stroke_width=3, stroke_fill=(30, 18, 8, 200))
        if small:
            d.text((W / 2, 300), small, font=font(58), fill=ACCENT + (255,), anchor="mm",
                   stroke_width=3, stroke_fill=(30, 18, 8, 200))
        _txt[(big, small)] = L
    return _txt[(big, small)]

def with_alpha(layer, k):
    if k >= 1: return layer
    c = layer.copy(); c.putalpha(c.getchannel("A").point(lambda v: int(v * k))); return c

def paste_pop(img, layer, u, dur):
    """文字在镜头切入后第 0.1 秒弹出：从 60% 放大到 108% 再回到 100%，镜头结束前淡出。"""
    p = (u - 0.1) / 0.28
    if p <= 0: return
    p = clamp(p)
    s = 0.6 + 0.48 * ease(p / 0.7) if p < 0.7 else 1.08 - 0.08 * ease((p - 0.7) / 0.3)
    a = min(clamp(p / 0.4), clamp((dur - u) / 0.12))
    L = layer if abs(s - 1) < 1e-3 else layer.resize((int(layer.width * s), int(layer.height * s)), Image.BICUBIC)
    L = with_alpha(L, a)
    img.paste(L, ((W - L.width) // 2, TEXT_Y - L.height // 2), L)

_end = None
def end_layer():
    global _end
    if _end is None:
        _end = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(_end)
        d.text((W / 2, 800), "早安", font=font(230, SERIF), fill=(255, 236, 205, 255), anchor="mm")
        d.line((W / 2 - 90, 965, W / 2 + 90, 965), fill=ACCENT + (230,), width=4)
        d.text((W / 2, 1040), "一杯手冲咖啡", font=font(60), fill=(255, 255, 255, 235), anchor="mm")
        d.text((W / 2, 1125), "15 g  ·  92°C  ·  1 : 15", font=font(44), fill=ACCENT + (235,), anchor="mm")
    return _end

def frame_at(T):
    n = len(DUR)
    i = min(n - 1, max(k for k in range(n) if ST[k] <= T + 1e-9))
    img = scene_frame(i, T)                    # 硬切，不叠化
    u = T - ST[i]
    big, small = SHOTS[i][4], SHOTS[i][5]
    if big: paste_pop(img, text_layer(big, small), u, DUR[i])
    k = ease((T - END_AT) / 0.6)               # 片尾：压暗 + 标题
    if k > 0:
        img = Image.blend(img, Image.new("RGB", (W, H), (14, 8, 3)), 0.6 * k)
        t = with_alpha(end_layer(), k); img.paste(t, (0, 0), t)
    f = min(clamp(T / 0.4), clamp((TOTAL - T) / 0.6))
    if f < 1: img = Image.blend(Image.new("RGB", (W, H), (0, 0, 0)), img, f)
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
        sheet = Image.new("RGB", (len(ts) * 360, 640))
        for k, t in enumerate(ts):
            sheet.paste(frame_at(t).resize((360, 640), Image.LANCZOS), (k * 360, 0))
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
        print("done", round(TOTAL, 2), [round(x, 2) for x in ST])
