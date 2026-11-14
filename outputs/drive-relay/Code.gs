// GitHub → Google Drive 中转。贴到 script.google.com 的项目里，给 sync 加一个定时触发器。
// Claude 把文件推到 claudework 仓库 drive-inbox 分支的 to-drive/ 里，并更新 index.json；
// 这个脚本读 index.json，把还没存过的文件复制到 Drive 的「Claude中转」文件夹。
const RAW = 'https://raw.githubusercontent.com/EosVesper/ClaudeWork/drive-inbox/to-drive/';
const FOLDER = 'Claude中转';

function sync() {
  const props = PropertiesService.getScriptProperties();
  const done = JSON.parse(props.getProperty('done') || '{}');
  const list = JSON.parse(UrlFetchApp.fetch(RAW + 'index.json?t=' + Date.now()).getContentText());
  const found = DriveApp.getFoldersByName(FOLDER);
  const folder = found.hasNext() ? found.next() : DriveApp.createFolder(FOLDER);
  const current = {};
  list.forEach(function (f) {
    current[f.id] = true;
    if (done[f.id]) return;
    const blob = UrlFetchApp.fetch(RAW + encodeURIComponent(f.name)).getBlob().setName(f.name);
    folder.createFile(blob);
    done[f.id] = true;
    props.setProperty('done', JSON.stringify(done));
    console.log('已保存：' + f.name);
  });
  Object.keys(done).forEach(function (k) { if (!current[k]) delete done[k]; });
  props.setProperty('done', JSON.stringify(done));
}
