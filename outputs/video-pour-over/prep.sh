#!/bin/sh
# 按 timing.py 把每个镜头要用的那一段抽成 30fps 的帧：fr2/<镜头>_NNN.jpg
# 慢放的镜头用 minterpolate 补帧，其余直接变速
mkdir -p fr2
python3 -c "
from timing import SHOTS, BEAT
for name, n, ss, use, *_ in SHOTS:
    print(name, ss, use, use / (n * BEAT))
" | while read name ss use speed; do
  slow=$(python3 -c "print(1 if $speed < 0.95 else 0)")
  if [ "$slow" = 1 ]; then vf="setpts=PTS/$speed,minterpolate=fps=30:mi_mode=mci:mc_mode=aobmc:vsbmc=1"
  else vf="setpts=PTS/$speed,fps=30"; fi
  ffmpeg -nostdin -y -loglevel error -ss $ss -t $use -i src/$name.mp4 -vf "$vf" -q:v 2 fr2/${name}_%03d.jpg
  echo "$name speed=$speed frames=$(ls fr2/${name}_* | wc -l)"
done
