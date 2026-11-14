"""重建 to-drive/index.json：列出要送进 Drive 的文件。每次增删文件后运行。"""
import hashlib, json, os
d = "to-drive"
out = []
for n in sorted(os.listdir(d)):
    if n == "index.json": continue
    p = os.path.join(d, n)
    h = hashlib.sha256(open(p, "rb").read()).hexdigest()[:12]
    out.append({"name": n, "size": os.path.getsize(p), "id": h + ":" + n})
json.dump(out, open(os.path.join(d, "index.json"), "w"), ensure_ascii=False, indent=1)
print(out)
