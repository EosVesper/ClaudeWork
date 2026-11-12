"""没有配音时：按 4.5 字/秒 + 句间停顿估算时间轴，写出 timing.json。"""
import json, re

RATE, PAUSE, LEAD = 4.5, 0.35, 0.3
# 每个场景对应的口播句
SCENE_LINES = [
    ["傍晚的天空|为什么会烧起来？"],
    ["阳光看起来是白的|其实混着所有颜色", "颜色不同，波长就不同"],
    ["白天，短波的蓝光|最容易被空气撞散|天就成了蓝色"],
    ["到了傍晚，阳光斜着进来|要穿过厚得多的大气"],
    ["一路上，蓝光几乎被散光了"],
    ["剩下的红光和橙光|把云染成了火"],
    ["所以晚霞不是云在变色|是空气在替你筛光"],
]

def chunks(line):
    # 按逗号/句号切成字幕条，问号保留
    return line.split("|")

t = LEAD
scene_dur, caps = [], []
for k, lines in enumerate(SCENE_LINES):
    s0 = 0.0 if k == 0 else t
    for line in lines:
        for c in chunks(line):
            n = len(re.sub(r"[，？]", "", c))
            d0 = 0.15 if "，" in c else 0
            d = n / RATE + 0.12 + d0
            caps.append([round(t, 3), round(t + d, 3), c])
            t += d
        t += PAUSE
    scene_dur.append(round(t - s0, 3))
json.dump({"scene_dur": scene_dur, "end_dur": 2.6, "captions": caps,
           "out": "sunset_draft.mp4"},
          open("timing.json", "w"), ensure_ascii=False, indent=1)
print(scene_dur, round(sum(scene_dur) + 2.6, 2))
for c in caps: print(c)
