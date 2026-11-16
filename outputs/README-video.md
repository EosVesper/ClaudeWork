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

## video-rainy-kitten/ — 练习 4《雨天的小橘猫》
同一只猫的 5 个图生视频镜头（角色一致性 + 叙事分镜）。
- `timing.py`：时间轴。把 15.8 秒的整段配音按句切开，重新摆到 25.7 秒的时间轴上；镜头时长、叠化、字幕都从这里算
- `render.py`：慢推运镜（`CAM`）、冷暖调色（`GRADE`）、逐句字幕、片尾；先把片段抽帧到 `fr/`，用法同前
- `audio.py`：`python3 audio.py voice.mp3` 重排配音，并用 numpy 合成室外/室内两种雨声和八音盒配乐，输出 `mix.m4a`
- 合成：`ffmpeg -i video.mp4 -i mix.m4a -map 0:v -map 1:a -c copy out.mp4`
- v2（代码补救素材瑕疵）：S2、S3 只取没出问题的一小段，用 `minterpolate` 放慢补帧（命令在 `render.py` 开头）；S3、S4 用 `CAM` 推近，把伞面、背景小伞、蓝袖子裁出画面

## video-pour-over/ — 练习 5《一杯手冲咖啡》
8 个图生视频镜头的卡点快剪，没有配音，画面文字 + 代码合成的 100 BPM 配乐。
- `timing.py`：每个镜头占几拍、用素材的哪一段、屏幕上的字；改这里就能换节奏和文案
- `prep.sh`：按 `timing.py` 把每段素材变速抽帧到 `fr2/`（慢放的用 `minterpolate` 补帧）
- `render.py`：硬切 + 切入时"顿"一下的运镜 + 弹出文字 + 片尾，用法同前
- `audio.py`：`python3 audio.py` 合成鼓、贝斯、电钢琴、旋律和动作音效，输出 `mix.m4a`
- 合成：`ffmpeg -i video.mp4 -i mix.m4a -map 0:v -map 1:a -c copy out.mp4`
