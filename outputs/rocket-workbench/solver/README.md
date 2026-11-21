# 火箭推力室化学平衡与理想性能计算

本目录把燃烧室、喷管及燃烧产物物性计算放在同一个工程中。原有的“燃烧室出口状态接口”和“other parts”目录未被改动。压强输入均为**绝对压强，单位 Pa**；温度为 K；标准压强 `p_0=101325 Pa`。

## 目录

| 路径 | 作用 |
| --- | --- |
| `src/chamber/` | NASA 热力数据读取、入口焓、元素守恒、平衡组成、能量方程和燃烧室出口状态 |
| `src/nozzle/` | 喉部、喷管截面与沿程流动、出口组成、理想性能及结果导出 |
| `src/products/` | 在已求得组成上计算凝相质量分数、冻结/平衡比热和气相输运性质 |
| `src/presets/` | 液体发动机基准工况；提供供命令行和前端共用的型号查询接口 |
| `apps/` | 液发、固发及燃烧室—喷管一体化命令行入口 |
| `thermo.inp`、`trans.inp` | NASA 热力和输运性质数据库 |
| `vendor/coolprop/` | 液氧、液氢、液甲烷入口状态计算所需的 CoolProp 库及许可证 |
| `build.ps1` | 统一编译脚本 |

主接口分别位于 `src/chamber/chamber_high_level.h`、`src/nozzle/nozzle_high_level.h` 和 `src/products/product_properties.h`。`ChamberOutlet` 保存燃烧室出口的压强、温度、焓、熵及每个候选物种的编号、相态、摩尔质量和摩尔数；`NozzleResult` 保存喉部、出口、沿程、性能和补充产物物性。调用 `nozzle_solve` 后须调用 `nozzle_result_free`，并在释放前读取 `result.balance` 中的物种信息。若单独调用 `product_properties_evaluate`，须先加载 `thermo.inp` 并准备与摩尔数顺序一致的 `MassBalance`；通过 `nozzle_solve` 调用则由总流程处理。

型号预设接口在 `src/presets/engine_presets.h`：`engine_preset_count`、`engine_preset_at`、`engine_preset_find` 用于列举和查找；`engine_preset_apply` 将基准值填入 `LiquidChamberInput` 和 `NozzleInput`，随后可修改任意工况字段再调用求解器。

## 编译与运行

在本目录的 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

需有 GCC/G++。脚本优先使用本机常见的 MinGW 路径，未找到时使用 `PATH` 中的编译器。编译得到 `liquid_chamber.exe`、`solid_chamber.exe`、`rocket_thermo.exe`。请从本目录运行，以便读取数据库和 CoolProp 文件。

```powershell
.\rocket_thermo.exe LOX LCH4 90 111 1 3.6 30000000 --eps 10
.\rocket_thermo.exe --list-engines
.\rocket_thermo.exe --engine TQ-12B
.\rocket_thermo.exe --engine SSME --Tox 92 --Tfuel 21 --of 6 --pc 20725640 --mdot 0 --pa 0
.\rocket_thermo.exe --solid JPN 298.15 7000000 --eps 12
.\rocket_thermo.exe LOX LCH4 90 111 1 3.6 30000000 --eps 10 --profile 25 --csv profile.csv --json result.json
```

液发位置参数依次是氧化剂、燃料、两者入口温度、总质量流量（kg/s）、氧燃质量比和燃烧室压强。固发位置参数是 `--solid`、配方、入口温度和燃烧室压强。可用代号：氧化剂 `LOX`、`N2O4`、`H2O2`；燃料 `LH2`、`LCH4`、`MMH`、`UDMH`、`RP1`、`N2H4`、`CH3OH`、`C2H5OH`；固体配方 `531`（聚氨酯）、`518`（聚硫）和 `JPN`。各组元的入口温度必须落在对应物性模型的适用区间内。

### 液体发动机型号预设

`--engine 代号` 自动填入推进剂、入口温度、主推力室流量、氧燃比、室压、面积比和环境压强。下表的温度是计算用入口初值，应按所分析的供给状态调整；质量流量为 `0` 时，程序只计算比性能，不输出绝对推力和喷管尺寸。型号选定后可用 `--Tox`、`--Tfuel`、`--mdot`、`--of`、`--pc`、`--eps`、`--pa` 等选项覆盖基准值。`--list-engines` 列出可用代号。

| 代号 | 推进剂 | 氧燃比 | 室压（Pa） | 面积比 | 主推力室流量（kg/s） | 参数来源 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `TQ-12B` | LOX/LCH4 | 2.9 | 14000000 | 20.5 | 316.704 | 《第一次大作业报告》表4～5 |
| `TQ-15B` | LOX/LCH4 | 2.9 | 14000000 | 50 | 307.580 | 《第一次大作业报告》表7～8；基准环境为真空 |
| `YF-100K` | LOX/RP1 | 2.6 | 18700000 | 35 | 422.583 | 《第一次大作业报告》表10～11；系列共用参考模型 |
| `SSME` | LOX/LH2 | 6.0 | 20725640 | 77.5 | 0 | [NASA SSME技术报告](https://ntrs.nasa.gov/api/citations/19910014930/downloads/19910014930.pdf)，100%额定工况 |
| `MERLIN-1D+` | LOX/RP1 | 2.32 | 11000000 | 16 | 0 | [Merlin 1D+参数表](https://stax.strath.ac.uk/downloads/s1784m21v?locale=en)，表5.11 |
| `RAPTOR-2` | LOX/LCH4 | 3.6 | 30000000 | 32 | 663.4 | [DLR Raptor 2参数表](https://elib.dlr.de/221988/1/EUCASS2025-641.pdf)，表1 |
| `RL10A-3-3` | LOX/LH2 | 5.0 | 2723430 | 57 | 0 | [NASA收录的RL10型号参数表](https://ntrs.nasa.gov/api/citations/19910018879/downloads/19910018879.pdf) |
| `VULCAIN-2` | LOX/LH2 | 6.10 | 11500000 | 58.5 | 0 | [ESA Vulcain 2技术资料](https://www.esa.int/esapub/bulletin/bullet102/Coulon102.pdf)，表1 |

TQ-12B、TQ-15B 的流量采用报告中的**主推力室**流量；燃气发生器支路不在本程序的燃烧室模型中。报告中的推力、比冲和效率也不作为当前理想热力求解器的输入，因而程序计算的理想推力不应直接视为整机实测推力。其余型号的入口温度和流量均可按实际工况重新指定。

`rocket_thermo.exe` 默认按绝热燃烧、化学平衡膨胀、声速喉部、出口面积比 40 和环境压强 101325 Pa 计算。常用选项：

| 选项 | 含义 |
| --- | --- |
| `--case 1`、`--case 2 Q`、`--case 3 Q` | 液发绝热、每 kg 燃料吸热、每 kg 产物散热；`Q` 的单位分别按后两种情形为 J/kg |
| `--cp-binder C`、`--cp-eff C` | 固发黏合剂或整剂等效比热，J/(kg·K)；前者仅适用于 `531`、`518` |
| `--Tox T`、`--Tfuel T` | 液发氧化剂和燃料入口温度，K |
| `--of K`、`--pc P` | 液发主推力室氧燃质量比、室压（Pa） |
| `--mdot M` | 主推力室质量流量，kg/s；液发可传 `0` 表示未知，固发必须大于 `0` |
| `--eps E`、`--pe P`、`--pr R`、`--mach M` | 按面积比、出口压强（Pa）、出口与室压之比或出口马赫数指定截面 |
| `--model eq/fc/fa/sf` | 平衡、组分冻结、完全冻结、突然冻结膨胀 |
| `--throat sonic/gamma/massflux` | 声速、临界压强比或最大质量流率喉部判据 |
| `--pa P`、`--profile N` | 环境压强（Pa）、沿程截面数 |
| `--Tmin T`、`--Tmax T` | 喷管候选产物表温度窗；默认下限 200 K、上限不低于 6000 K |
| `--thermo FILE`、`--trans FILE` | 指定 NASA 热力或输运数据库文件 |
| `--all`、`--csv FILE`、`--json FILE` | 显示零含量候选组分，输出沿程 CSV 或完整 JSON |

仅算燃烧室时可使用 `liquid_chamber.exe` 和 `solid_chamber.exe`，具体参数可直接运行程序查看用法。JSON 的 `schema_version` 为 `1.2`，其中 `product_properties` 包含燃烧室和喷管出口的补充物性。燃烧室出口及 JSON 的 `chamber` 中还给出 `temperature_evaluations`（含搜区间和复算的温度试算次数）、`brent_iterations`（温度根迭代次数）、`equilibrium_iterations`（最终温度处的平衡迭代次数）和 `phase_iterations`（最终温度处的凝相调整轮数）。

## 输出与适用边界

一体化程序输出燃烧室出口状态、喉部和喷管出口的压强/温度/流速/马赫数/组成、沿程状态（可选）、特征速度、推力系数、比冲及已知质量流量时的推力和几何尺寸。补充物性包括凝相质量分数、平均摩尔质量、冻结与平衡定压/定容比热、比热比，以及 `trans.inp` 覆盖足够时的气相黏度、导热系数和普朗特数。补充物性有单独的有效标志；某项数据不可用时不会使主热力计算失效。

目前不考虑电离。喷管性能是理想热力性能，未计燃烧效率、壁面传热、摩擦、非一维流动及喷管分离。平衡比热是在当前活动凝相集合不变时求得的局部导数；相变点附近可能不可用。输运性质采用数据库单组分拟合及气相混合近似，适合初步分析。
