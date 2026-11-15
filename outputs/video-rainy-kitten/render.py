"""练习 4《雨天的小橘猫》：5 个图生视频镜头 + 慢推运镜 + 冷暖调色 + 逐句字幕 + 片尾。
准备（抽帧到 fr/）：
  S1 S4 S5 原速：ffmpeg -i src/sN.mp4 -vf fps=30 -q:v 2 fr/cN_%03d.jpg
  S2 只用前 1.3 秒（之后猫头变大、穿进盒盖），放慢到 0.35 倍并补帧：
    ffmpeg -t 1.3 -i src/s2.mp4 -vf "setpts=PTS/0.35,minterpolate=fps=30:mi_mode=mci:mc_mode=aobmc:vsbmc=1" -q:v 2 fr/c2_%03d.jpg
  S3 只用 0.8~3.65 秒（雨靴走进来那段），放慢到 0.55 倍并补帧：
    ffmpeg -ss 0.8 -t 2.85 -i src/s3.mp4 -vf "setpts=PTS/0.55,minterpolate=fps=30:mi_mode=mci:mc_mode=aobmc:vsbmc=1" -q:v 2 fr/c3_%03d.jpg
  python3 render.py preview t1 t2 ...   -> contact.png
  python3 render.py render [场景号...]  -> video.mp4（无声）
时间轴（场景时长、配音放置、字幕）都在 timing.py 里，audio.py 共用。
"""
import os, subprocess, sys
from multiprocessing import Pool
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from timing import DUR, XF, CAPS, TITLE_AT, TOTAL, ST

W, H, FPS = 1080, 1920, 30
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
SERIF = "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc"
CAP_Y = 1400
# 每个镜头：(起始缩放, 结束缩放, 焦点x, 焦点y)  焦点是画面上保持不动的点（0~1）
CAM = [(1.00, 1.10, 0.68, 0.74),   # S1 远景，慢慢推向纸箱
       (1.06, 1.00, 0.50, 0.40),   # S2 轻微拉远，带出窗户
       (1.27, 1.33, 0.60, 1.00),   # S3 裁掉上面 1/5：伞面和握伞的手出画，只留伞柄和雨靴
       (1.33, 1.38, 0.28, 1.00),   # S4 推近到猫脸和指尖：背景的小伞、蓝袖子出画
       (1.08, 1.00, 0.50, 0.60)]   # S5 拉远收尾
# 调色：(R, G, B 增益, 饱和度)  前四镜偏冷，S5 偏暖
GRADE = [(0.95, 1.0, 1.06, 0.90)] * 4 + [(1.06, 1.0, 0.90, 1.08)]

def font(s, path=FONT, _c={}):
    if (s, path) not in _c: _c[(s, path)] = ImageFont.truetype(path, int(s), index=2)
    return _c[(s, path)]
def clamp(x): return max(0.0, min(1.0, x))
def ease(x):
    x = clamp(x); return x * x * (3 - 2 * x)

_n = {}
def nframes(i):
    if i not in _n: _n[i] = len([f for f in os.listdir(os.path.join(HERE, "fr")) if f.startswith(f"c{i + 1}_")])
    return _n[i]

_lut = {}
def grade(img, i):
    r, g, b, sat = GRADE[i]
    if i not in _lut:
        _lut[i] = sum(([min(255, int(v * k)) for v in range(256)] for k in (r, g, b)), [])
    img = img.point(_lut[i])
    if sat != 1:
        img = Image.blend(img.convert("L").convert("RGB"), img, sat)
    return img

def scene_frame(i, T):
    u = T - ST[i] + XF[i]                     # 每个镜头提前 XF 秒开始，用于叠化
    k = min(nframes(i), max(1, int(u * FPS) + 1))
    src = Image.open(os.path.join(HERE, "fr", f"c{i + 1}_{k:03d}.jpg")).convert("RGB")
    z0, z1, fx, fy = CAM[i]
    z = z0 + (z1 - z0) * ease(u / (DUR[i] + XF[i]))
    w, h = src.size
    S = W / w * z
    ww, wh = W / S, H / S
    x0, y0 = (w - ww) * fx, (h - wh) * fy
    img = src.transform((W, H), Image.AFFINE, (1 / S, 0, x0, 0, 1 / S, y0), resample=Image.BICUBIC)
    return grade(img, i)

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

_title = None
def title_layer():
    global _title
    if _title is None:
        _title = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(_title)
        d.text((W / 2, 800), "雨天的小橘猫", font=font(120, SERIF), fill=(255, 236, 205, 255), anchor="mm")
        d.line((W / 2 - 90, 910, W / 2 + 90, 910), fill=(255, 200, 120, 220), width=4)
        d.text((W / 2, 985), "后来，它有家了", font=font(54), fill=(255, 255, 255, 230), anchor="mm")
    return _title

def with_alpha(layer, k):
    if k >= 1: return layer
    c = layer.copy(); c.putalpha(c.getchannel("A").point(lambda v: int(v * k))); return c

def frame_at(T):
    n = len(DUR)
    i = min(n - 1, max(k for k in range(n) if ST[k] <= T + 1e-9))
    img = scene_frame(i, T)
    if i + 1 < n and ST[i + 1] - T < XF[i + 1]:
        img = Image.blend(img, scene_frame(i + 1, T), ease(1 - (ST[i + 1] - T) / XF[i + 1]))
    # 片尾：画面压暗 + 标题
    k = ease((T - TITLE_AT) / 0.8)
    if k > 0:
        img = Image.blend(img, Image.new("RGB", (W, H), (10, 6, 2)), 0.55 * k)
        t = with_alpha(title_layer(), k); img.paste(t, (0, 0), t)
    for s, e, text in CAPS:
        if s <= T < e:
            c = with_alpha(cap_img(text), min(1.0, (T - s) / 0.12, (e - T) / 0.12))
            img.paste(c, (0, CAP_Y - 100), c)
    # 开头淡入、结尾淡出
    f = min(clamp(T / 0.6), clamp((TOTAL - T) / 0.7))
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
        print("done", round(TOTAL, 2), ST)
