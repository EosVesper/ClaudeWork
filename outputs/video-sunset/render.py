"""晚霞短片渲染：图片推拉运镜 + 叠化 + 烧录中文字幕 + 片尾字卡。
用法：
  python3 render.py preview t1 t2 ...   # 导出这些时间点的单帧 PNG
  python3 render.py render              # 渲染全部场景并拼接
时间轴来自 timing.json（没有配音时按语速估算；有配音后用实测时间覆盖）。
"""
import json, os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, FPS = 1080, 1920, 30
XF = 0.4  # 叠化时长（秒）
UP = "/mnt/user-data/uploads/"
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"

# 图片、聚焦点（归一化坐标）、推(+1)/拉(-1)
SCENES = [
    ("8c046824-image.png", (0.5, 0.70), +1),  # 城市晚霞
    ("fd185b0c-image.png", (0.55, 0.55), -1),  # 三棱镜
    ("525ccde2-image.png", (0.5, 0.40), +1),  # 蓝天
    ("f6cb17fa-image.png", (0.5, 0.64), +1),  # 太空看大气层
    ("0d17b3c6-image.png", (0.5, 0.75), -1),  # 黄昏渐变
    ("78699c06-image.png", (0.5, 0.50), +1),  # 云层特写
    ("0b31a263-image.png", (0.5, 0.60), -1),  # 雪山湖面
]
END_TEXT = ["晚霞", "是空气筛出来的颜色"]

timing = json.load(open(os.path.join(HERE, "timing.json")))
# scene_dur: 每个图片场景的时长；end_dur: 片尾字卡；captions: [起, 止, 文本]
scene_durs = timing["scene_dur"] + [timing["end_dur"]]
starts = [0.0]
for d in scene_durs:
    starts.append(starts[-1] + d)
TOTAL = starts[-1]
captions = timing["captions"]

_src = {}
def src(i):
    if i not in _src:
        _src[i] = Image.open(UP + SCENES[i][0]).convert("RGB")
    return _src[i]

def scene_frame(i, u):
    """场景 i 在局部时间 u（0..dur+XF）时的画面。"""
    if i == len(SCENES):
        return end_card(u)
    im = src(i)
    w, h = im.size
    span = scene_durs[i] + XF
    p = max(0.0, min(1.0, u / span))
    _, (fx, fy), d = SCENES[i]
    a = p if d > 0 else 1 - p          # a=0 原始构图，a=1 推到 108%
    z = 1.0 + 0.08 * a
    S = max(W / w, H / h) * z
    ww, wh = W / S, H / S              # 源图里的取景框大小
    cx = w / 2 + (fx * w - w / 2) * a
    cy = h / 2 + (fy * h - h / 2) * a
    cx = min(max(cx, ww / 2), w - ww / 2)
    cy = min(max(cy, wh / 2), h - wh / 2)
    return im.transform((W, H), Image.AFFINE,
                        (1 / S, 0, cx - ww / 2, 0, 1 / S, cy - wh / 2),
                        resample=Image.BICUBIC)

_end = None
def end_card(u):
    global _end
    if _end is None:
        _end = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        dr = ImageDraw.Draw(_end)
        f1 = ImageFont.truetype(FONT, 150, index=2)
        f2 = ImageFont.truetype(FONT, 64, index=2)
        dr.text((W / 2, 880), END_TEXT[0], font=f1, fill=(255, 140, 70, 255), anchor="mm")
        dr.text((W / 2, 1040), END_TEXT[1], font=f2, fill=(255, 255, 255, 235), anchor="mm")
    base = Image.new("RGB", (W, H), (0, 0, 0))
    k = max(0.0, min(1.0, (u - XF) / 0.6))  # 叠化进黑底后，文字再淡入
    if k > 0:
        layer = _end.copy()
        layer.putalpha(layer.getchannel("A").point(lambda v: int(v * k)))
        base.paste(layer, (0, 0), layer)
    return base

_cap = {}
def cap_img(text):
    if text not in _cap:
        f = ImageFont.truetype(FONT, 66, index=2)
        layer = Image.new("RGBA", (W, 200), (0, 0, 0, 0))
        dr = ImageDraw.Draw(layer)
        dr.text((W / 2, 100), text, font=f, fill=(0, 0, 0, 170), anchor="mm")
        shadow = layer.filter(ImageFilter.GaussianBlur(10))
        dr2 = ImageDraw.Draw(shadow)
        dr2.text((W / 2, 100), text, font=f, fill=(255, 255, 255, 255), anchor="mm",
                 stroke_width=4, stroke_fill=(15, 15, 15, 235))
        _cap[text] = shadow
    return _cap[text]

CAP_Y = 1330  # 字幕中心约在画面 70% 高度，避开平台底部按钮区

def frame_at(T):
    # 找到当前场景
    i = max(k for k in range(len(scene_durs)) if starts[k] <= T + 1e-9)
    i = min(i, len(scene_durs) - 1)
    u = XF + (T - starts[i]) if i > 0 else T + XF
    img = scene_frame(i, u)
    tail = starts[i + 1] - T
    if i + 1 < len(scene_durs) and tail < XF:
        nxt = scene_frame(i + 1, XF - tail)
        img = Image.blend(img, nxt, 1 - tail / XF)
    for s, e, text in captions:
        if s <= T < e:
            k = min(1.0, (T - s) / 0.12, (e - T) / 0.12)
            c = cap_img(text)
            if k < 1:
                c = c.copy(); c.putalpha(c.getchannel("A").point(lambda v: int(v * k)))
            img.paste(c, (0, CAP_Y - 100), c)
    return img

def render():
    os.makedirs(os.path.join(HERE, "parts"), exist_ok=True)
    f0 = [round(s * FPS) for s in starts]
    names = []
    for i in range(len(scene_durs)):
        out = os.path.join(HERE, "parts", f"s{i:02d}.mp4")
        tmp = out[:-4] + ".tmp.mp4"
        p = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo",
                              "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
                              "-c:v", "libx264", "-crf", "18", "-preset", "veryfast",
                              "-pix_fmt", "yuv420p", "-r", str(FPS), tmp], stdin=subprocess.PIPE)
        for n in range(f0[i], f0[i + 1]):
            p.stdin.write(frame_at(n / FPS).tobytes())
        p.stdin.close()
        if p.wait() != 0:
            sys.exit(f"scene {i} failed")
        os.replace(tmp, out)
        names.append(out)
        print("scene", i, "done", flush=True)
    lst = os.path.join(HERE, "parts", "list.txt")
    open(lst, "w").write("".join(f"file '{n}'\n" for n in names))
    final = os.path.join(HERE, timing.get("out", "sunset_draft.mp4"))
    tmp = final[:-4] + ".tmp.mp4"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0",
                    "-i", lst, "-c", "copy", "-movflags", "+faststart", tmp], check=True)
    os.replace(tmp, final)
    print("TOTAL", round(TOTAL, 2), "->", final)

if __name__ == "__main__":
    if sys.argv[1] == "preview":
        for t in sys.argv[2:]:
            frame_at(float(t)).save(os.path.join(HERE, f"preview_{t}.png"))
    else:
        render()
