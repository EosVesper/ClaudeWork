# 版本记录

| 版本 | 提交 | 日期 | 说明 |
| --- | --- | --- | --- |
| v1.2.0（展示版） | `686de29` | 2026-11-22 | 数据驱动的喷管主视觉、点火主题、动效 |
| v1.1.0（交付版） | `17fb139` | 2026-11-22 | 交付说明、验收清单、界面修复；中文文件名带 UTF-8 标记 |
| v1.0.0（ChatGPT 原版基线） | `6066e90` | 2026-11-21 | 评审时的原始版本，只加了 Wine 启动方式 |

取回某个版本：

- 网页：在仓库的 Commits 列表里点对应的提交，再点「Browse files」。
- 命令行：`git checkout <提交> -- outputs/rocket-workbench`

两个大 DLL（`CoolProp.dll`、`libstdc++-6.dll`）不在仓库里，运行时请从对应版本的压缩包里取。

## 建议的 Git 标签（需由仓库所有者创建）

```
git tag -a rocket-workbench-v1.1.0 17fb139 -m "交付版"
git tag -a rocket-workbench-v1.2.0 686de29 -m "展示版"
git push origin rocket-workbench-v1.1.0 rocket-workbench-v1.2.0
```
