# 视频渲染脚本

两支练习短片的渲染脚本（Python + Pillow + ffmpeg，1080×1920，30fps）。

## video-sunset/ —《为什么晚霞是红色的》
AI 图片 + 配音 → 推拉运镜、叠化、烧录字幕、片尾字卡。
- `make_timing.py`：没有配音时，按语速估算时间轴
- `make_timing_voice.py`：有配音后，按 `ffmpeg silencedetect` 量出的停顿重排时间轴
- `render.py`：读 `timing.json` 渲染；`python3 render.py preview 1.5 9.7` 看单帧，`python3 render.py render` 出片

## video-compound/ —《每天进步 1%》
纯代码动画（柱状图、曲线、计数器），不需要图片。
- `render.py`：台词时间写在 `CAPS` 和各场景函数里；用法同上

## 换新片子时要改的地方
- 图片路径（`UP`、`SCENES`）和字体路径（`FONT`）
- 字幕与时间轴（`timing.json` 或 `CAPS`）
- 配音合成：`ffmpeg -i video.mp4 -i voice.mp3 -map 0:v -map 1:a -c:v copy -af loudnorm=I=-15:TP=-1.5 -c:a aac -shortest out.mp4`

## video-sunset-v2/ — 晚霞短片第二版
图生视频片段（先用 ffmpeg 裁成竖屏并抽帧到 `fr/`）+ 棱镜静态图 + 代码画的大气层示意动画。
- `render.py`：渲染画面；`python3 render.py render 3` 可只重渲某个场景
- `music.py`：用 numpy 合成氛围铺底音乐，`python3 music.py 29.71` 生成 `music.wav`
- 混音：人声 loudnorm 到 -15 LUFS，音乐 `volume=0.22` 并用 `sidechaincompress` 在人声出现时压低
