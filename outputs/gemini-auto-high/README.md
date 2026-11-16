# Gemini 自动切到 High 思考档（iPad Safari + Stay）

接手自 Gemini 那段没成功的对话。

## 之前几版为什么失败
1. **MutationObserver 每次 DOM 变化都重跑整套流程**：菜单展开动画本身会产生 DOM 变化，于是又排队点击 “Thinking level”。结果是刚展开就被收起，你看到的就是“只展开了一级”。
2. **v4 的 `safeClick` 一次点两下**（`el.click()` 加上 `dispatchEvent('click')`），还会同时点卡片、外层和箭头。折叠面板被连续切换了好几次。
3. **用 `querySelectorAll('*').find(textContent…)` 找元素**：匹配到的是最外层大容器，而不是 “Thinking level” 那一行。

## v5 的做法（v5.1：修复 High 选项嵌在 Thinking level 菜单项内部时被误排除的问题）
- 只有一条顺序流程，带锁，不会并发，也不会重复点击。
- 按文字找到最小的叶子元素，再往上找真正可点击的元素（不会越出弹窗）。
- 每次只点一下，等结果出来再决定下一步。展开失败时依次尝试：整行、`aria-expanded` 元素、行里的箭头按钮、文字本身。
- 每一步之后都重新查找元素，不受页面重新渲染影响。
- 不会匹配聊天内容、侧边栏和输入框里的 “High” 字样。
- 如果已经是 High，就直接收起菜单。
- 选完后自动收起菜单。默认只在首次加载和新对话时执行（`ONLY_NEW_CHAT`）。
- `DEBUG = true` 时，右下角会显示日志框，有“复制诊断 / 重试 / 关闭”按钮。

## 安装（Stay）
1. 在 iPad 设置 → Safari → 扩展里，打开 **Stay** 和 **Stay - 1. Basic**，网站权限设为 gemini.google.com “允许”。
2. 在 Stay App 里新建脚本，粘贴 `gemini-auto-high.user.js` 的全部内容并保存。把之前那几版脚本删掉或关闭，以免互相打架。
3. 刷新 gemini.google.com，看右下角的日志。
4. 成功以后，把脚本里的 `DEBUG` 改成 `false`。

## 书签版（不装扩展）
新建一个 Safari 书签，把网址替换成 `bookmarklet.txt` 的全部内容。打开 Gemini 后点一下即可。

## 如果还是不行
在日志框里点“复制诊断”，把复制出来的内容发给 Claude。它包含菜单的真实 HTML，可以据此精确修正。
