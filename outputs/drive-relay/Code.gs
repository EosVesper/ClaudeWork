// GitHub → Google Drive 中转（第 2 版：支持大文件，每分钟检查一次）
// 需要：左侧「服务」里添加 Drive API；给 sync 加「每分钟」的时间触发器。
const REPO = 'EosVesper/ClaudeWork';
const BRANCH = 'drive-inbox';
const FOLDER = 'Claude Relay';

function sync() {
  const lock = LockService.getScriptLock();
  if (!lock.tryLock(1000)) return;              // 上一次还在传，就跳过
  try {
    const props = PropertiesService.getScriptProperties();
    // 1. 看分支有没有新提交（这个地址没有缓存，也不需要令牌）
    const refs = UrlFetchApp.fetch('https://github.com/' + REPO + '.git/info/refs?service=git-upload-pack').getContentText();
    const m = refs.match(new RegExp('([0-9a-f]{40}) refs/heads/' + BRANCH + '\\n'));
    if (!m || props.getProperty('sha') === m[1]) return;
    const raw = 'https://raw.githubusercontent.com/' + REPO + '/' + m[1] + '/to-drive/';
    // 2. 读清单，把没存过的文件传进 Drive
    const list = JSON.parse(UrlFetchApp.fetch(raw + 'index.json').getContentText());
    const done = JSON.parse(props.getProperty('done') || '{}');
    const found = DriveApp.getFoldersByName(FOLDER);
    const folder = found.hasNext() ? found.next() : DriveApp.createFolder(FOLDER);
    const current = {};
    list.forEach(function (f) {
      current[f.id] = true;
      if (done[f.id]) return;
      upload(raw, f, folder.getId());
      done[f.id] = true;
      props.setProperty('done', JSON.stringify(done));
      console.log('已保存：' + f.name + '（' + f.size + ' 字节）');
    });
    Object.keys(done).forEach(function (k) { if (!current[k]) delete done[k]; });
    props.setProperty('done', JSON.stringify(done));
    props.setProperty('sha', m[1]);
  } finally {
    lock.releaseLock();
  }
}

// 分片下载，再用 Drive 的断点续传接口拼成一个完整文件
function upload(raw, f, folderId) {
  const auth = { Authorization: 'Bearer ' + ScriptApp.getOAuthToken() };
  const init = UrlFetchApp.fetch('https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable', {
    method: 'post', contentType: 'application/json', headers: auth,
    payload: JSON.stringify({ name: f.name, parents: [folderId] })
  });
  const h = init.getHeaders();
  const url = h['Location'] || h['location'];
  let pos = 0;
  f.parts.forEach(function (p) {
    const bytes = UrlFetchApp.fetch(raw + p).getContent();
    const end = pos + bytes.length - 1;
    const r = UrlFetchApp.fetch(url, {
      method: 'put', payload: bytes, muteHttpExceptions: true,
      headers: { 'Content-Range': 'bytes ' + pos + '-' + end + '/' + f.size }
    });
    const code = r.getResponseCode();
    if (code !== 308 && code !== 200 && code !== 201) throw new Error('上传失败 ' + code + '：' + r.getContentText().slice(0, 300));
    pos = end + 1;
  });
}

// 运行一次即可：创建「每分钟运行 sync」的触发器（会先删掉本项目已有的触发器）
function setup() {
  ScriptApp.getProjectTriggers().forEach(function (t) { ScriptApp.deleteTrigger(t); });
  ScriptApp.newTrigger('sync').timeBased().everyMinutes(1).create();
}
