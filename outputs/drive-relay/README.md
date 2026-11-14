# GitHub → Google Drive 中转

连接器单次上传约 3MB 上限，超过的文件走这条通道。文件切成 20MB 分片，单个文件可以超过 100MB。

1. 在 `drive-inbox` 分支运行 `python3 relay.py add <文件> [Drive里的文件名]`，然后提交并推送。
2. Google Apps Script（`Code.gs`，每分钟运行 `sync`）发现分支有新提交，就按 `to-drive/index.json` 把分片下载并拼成完整文件，存进 Drive 的「Claude中转」文件夹。
3. 文件到 Drive 后，运行 `python3 relay.py clear` 并推送，清空中转站。

注意：仓库是公开的，放进 `to-drive/` 的文件在清理之前任何人都能访问。
