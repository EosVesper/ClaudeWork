# GitHub → Google Drive 中转

连接器单次上传约 3MB 上限，超过的文件走这条通道（单个文件上限约 50MB）。

1. Claude 把文件放进 `drive-inbox` 分支的 `to-drive/`，运行 `python3 update_index.py`，提交并推送。
2. Google Apps Script（`Code.gs`，由定时触发器运行 `sync`）读取 `to-drive/index.json`，把新文件复制到 Drive 的「Claude中转」文件夹。
3. 确认文件到了 Drive 之后，可以把 `to-drive/` 里的文件删掉并重建 index。

注意：仓库是公开的，放进 `to-drive/` 的文件在清理之前任何人都能访问。
