"""把文件放进中转站：python3 relay.py add <文件> [Drive里的文件名]   /   python3 relay.py clear
文件被切成 20MB 的分片存进 to-drive/parts/，清单写在 to-drive/index.json。
之后 git add -A && git commit && git push origin drive-inbox，Apps Script 会在一分钟内取走。"""
import hashlib, json, os, shutil, sys
D, P, CH = "to-drive", "to-drive/parts", 20 * 1024 * 1024   # 分片必须是 256KB 的整数倍
IDX = os.path.join(D, "index.json")
os.makedirs(P, exist_ok=True)
idx = json.load(open(IDX)) if os.path.exists(IDX) else []
if sys.argv[1] == "clear":
    shutil.rmtree(P); os.makedirs(P); idx = []
else:
    src = sys.argv[2]; name = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(src)
    data = open(src, "rb").read()
    h = hashlib.sha256(data).hexdigest()[:16]
    parts = []
    for k in range(0, max(len(data), 1), CH):
        pn = f"parts/{h}.{k // CH:03d}"
        open(os.path.join(D, pn), "wb").write(data[k:k + CH]); parts.append(pn)
    idx = [f for f in idx if f["id"] != h] + [{"id": h, "name": name, "size": len(data), "parts": parts}]
json.dump(idx, open(IDX, "w"), ensure_ascii=False, indent=1)
print(json.dumps(idx, ensure_ascii=False))
