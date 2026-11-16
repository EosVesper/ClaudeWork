"""练习 5《一杯手冲咖啡》v2 舒缓版：8 个镜头 + 叠化 + 匀速慢推 + 淡入文字 + 片尾。
（v1 卡点快剪版在 v1/ 里）
先运行 sh prep.sh 抽帧到 fr2/。
  python3 render.py preview t1 t2 ...   -> contact.png
  python3 render.py render [场景号...]  -> video.mp4（无声）
"""
import glob, os, subprocess, sys
from multiprocessing import Pool
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from timing import SHOTS, DUR, ST, TOTAL, XF, END_AT

W, H, FPS = 1080, 1920, 30
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
SERIF = "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc"
TEXT_Y = 1440
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
    u = T - ST[i] + (XF if i else 0)                              # 提前 XF 秒开始，用于叠化
    k = min(nframes(name), max(1, int(round(u * FPS)) + 1))      # 帧用完后停在最后一帧（C8 的定格）
    src = Image.open(os.path.join(HERE, "fr2", f"{name}_{k:03d}.jpg")).convert("RGB")
    z = 1 + 0.05 * u / (DUR[i] + XF)                             # 整镜匀速慢推 5%，没有任何突然的动作
    w, h = src.size
    S = W / w * z
    ww, wh = W / S, H / S
    return src.transform((W, H), Image.AFFINE, (1 / S, 0, (w - ww) / 2, 0, 1 / S, (h - wh) / 2),
                         resample=Image.BICUBIC)

_txt = {}
def text_layer(big, small):
    """大字（宋体）+ 小字，带柔和阴影，画在一张透明图上。"""
    if (big, small) not in _txt:
        L = Image.new("RGBA", (W, 360), (0, 0, 0, 0))
        d = ImageDraw.Draw(L)
        d.text((W / 2, 130), big, font=font(112, SERIF), fill=(0, 0, 0, 170), anchor="mm")
        if small: d.text((W / 2, 250), small, font=font(46), fill=(0, 0, 0, 170), anchor="mm")
        L = L.filter(ImageFilter.GaussianBlur(16))
        d = ImageDraw.Draw(L)
        d.text((W / 2, 130), big, font=font(112, SERIF), fill=(255, 244, 226, 255), anchor="mm")
        if small: d.text((W / 2, 250), small, font=font(46), fill=ACCENT + (255,), anchor="mm")
        _txt[(big, small)] = L
    return _txt[(big, small)]

def with_alpha(layer, k):
    if k >= 1: return layer
    c = layer.copy(); c.putalpha(c.getchannel("A").point(lambda v: int(v * k))); return c

def paste_text(img, layer, u, dur):
    """文字在镜头开始 0.3 秒后慢慢淡入（同时上浮 24 像素），镜头结束前 0.2 秒淡完。"""
    a = min(ease((u - 0.3) / 0.6), ease((dur - 0.2 - u) / 0.5))
    if a <= 0: return
    L = with_alpha(layer, a)
    y = TEXT_Y - L.height // 2 + int(24 * (1 - ease((u - 0.3) / 0.9)))
    img.paste(L, (0, y), L)

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
    img = scene_frame(i, T)
    if i + 1 < n and ST[i + 1] - T < XF:       # 和下一镜叠化
        img = Image.blend(img, scene_frame(i + 1, T), ease(1 - (ST[i + 1] - T) / XF))
    u = T - ST[i]
    big, small = SHOTS[i][4], SHOTS[i][5]
    if big: paste_text(img, text_layer(big, small), u, DUR[i])
    k = ease((T - END_AT) / 1.2)               # 片尾：压暗 + 标题
    if k > 0:
        img = Image.blend(img, Image.new("RGB", (W, H), (14, 8, 3)), 0.6 * k)
        t = with_alpha(end_layer(), k); img.paste(t, (0, 0), t)
    f = min(ease(T / 1.0), ease((TOTAL - T) / 1.2))
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
