# ARM64 加载 ARM32 Bytecode 变更日志（防回归）

最后更新：2026-03-22 11:05  
维护规则：每次改 `tolua.c` 或重编插件后，必须追加一条记录并更新回归矩阵；提交前必须执行 `tools/check_arm64_fr2_log.ps1`。

## 1. 当前目标

- 目标：在 `arm64` 上稳定加载 `arm32` LuaJIT bytecode。
- 当前主线问题：
1. `jygame/battle.lua`：`RegisterWorkflowForSkills` 仍报 `attempt to concatenate a table value`（已加针对性修复，待真机验证）。

## 2. 变更记录（按时间倒序）

| 日期 | Commit | 变更摘要 | 目标问题 | 当前结论 |
|---|---|---|---|---|
| 2026-03-22 | `待提交` | 新增 battle 定向门禁：校验 `RegisterWorkflowForSkills` 的 `CALL(C=6/C=4)` 调用点在 FR2 产物中均带正确 MOV 搬移链（防“脚本过、真机仍原错”） | battle `RegisterWorkflowForSkills` 旧错误反复 | 本地门禁通过（`hits=21`，`fail=0`） |
| 2026-03-22 | `待提交` | 在 root proto(`pflags=0x03`) 补回 `CALL(C=4)` 的 `TGETS+KSTR+TDUP+FNEW` 强制 `copy-fallback` | battle `RegisterWorkflowForSkills` 仍报 concat table | 离线反汇编已确认 C4/C6 全部插入 MOV 链，待真机 |
| 2026-03-22 | `待提交` | 收窄 `CALL(C=6)` 的 `KSTR+TDUP+FNEW+KSTR+KPRI` 强制 `copy-fallback`：仅在 root proto(`pflags=0x03`) 生效；避免函数体内大面积误命中 | battle `RegisterWorkflowForSkills` 拼接 table 报错 + 连锁副作用 | 离线通过；`battle copy_fallback_hits` 610→595，`rebuild_count` 4488→839，待真机 |
| 2026-03-22 | `待提交` | 在 `tolua_loadbuffer` 增加构建签名日志：`build=arm64fr2-20260322-rootcall6`，并对 `main/battle/migong` 打印 `conv_ok/conv_fail` | 真机无法确认是否加载最新 so | 已输出可观测标记，待真机回传日志 |
| 2026-03-22 | `cd134e7` | 新增 `CALL(C=6)` 的 `KSTR+TDUP+FNEW+KSTR+KPRI` 强制 `copy-fallback` 路径，规避 `RegisterWorkflowForSkills` 长链调用残留错位 | battle `RegisterWorkflowForSkills` 拼接 table 报错 | 离线转换通过，待真机 |
| 2026-03-22 | `cd134e7` | 更新 `docs/arm64_offline_regression_baseline.json`（battle 指纹变更：`kstr_tdup_rejects=5`、`copy_fallback_hits=240`） | 离线门禁跟随新策略 | 本地门禁已通过 |
| 2026-03-21 | `9aa9e73` | 新增 `tools/run_arm64_offline_regression.ps1` + `docs/arm64_offline_regression_baseline.json`，实现离线门禁（重编调试转换器、批量转换 main/battle/migong、日志指纹比对） | 降低每次改动都上真机的频率 | 本地脚本已跑通，可用于自主回归 |
| 2026-03-21 | `5a9ceaa` | 新增 `tools/check_arm64_fr2_log.ps1`，强制校验 `tolua.c/arm64 so` 改动必须同步更新日志 | 防止“改代码不记账”导致循环回归 | 已启用，本地检查通过 |
| 2026-03-21 | `5a9ceaa` | 新增两条窄规则：`MOV+MOV+TGET*+CALL(C=3)` 的 table-style 两参调用防错位；`KSTR+TDUP` 识别 `new_first` 误绑 `old_second` | `PatchMigong insert` 与 `RegisterWorkflowForSkills` 参数错位 | 本地反汇编已验证目标点修正，待真机 |
| 2026-03-21 | `bbb3af3` | 用 NDK r21e 重编 `arm64-v8a/libtolua.so` 并同步到插件目录 | 同步最新转换逻辑到真机 | 已发布，需结合真机日志判断 |
| 2026-03-21 | `c1ca748` | 收窄两参数 MOV 链规则，仅保留低寄存器形态（old src=0 且 old2 src=1/3）；移除过宽规则（`dense arg block`、`stale new-last`） | 避免“修 Log 时误伤其它调用链” | 部分回落，但仍有 battle/migong 两个主线错误 |
| 2026-03-21 | `b27ec3c` | 强制两参数立即调用准备路径走 repack（battle hooks） | battle 某些参数错位 | 修复局部，同时引入后续回归风险 |
| 2026-03-20 | `759fa45` | 修 ContainsKey 方法风格调用 self/key 参数顺序 | main.lua ContainsKey 参数类型错误 | 已改善 |
| 2026-03-20 | `6c477cb` | 避免 single-arg 调用复用 stale existing-slice | floor/单参调用错位 | 已改善 |

## 3. 回归矩阵（只保留高价值检查）

| 模块 | 关键点 | 预期 | 当前状态 |
|---|---|---|---|
| `main.lua` | `ContainsKey` 调用 | 参数1应为 Dictionary 实例 | 通过（已从主报错列表移除） |
| `migong.lua` | `split` 中 `table.insert` | 第1参数应为 table | 通过（旧问题已修） |
| `migong.lua` | `PatchMigong` 内 `table.insert` | 第1参数应为 table | 本地反汇编通过（`MOV 8/9`），待真机 |
| `battle.lua` | `CheckIfEquipNotRight` 调用 | 参数顺序正确 | 通过（已修） |
| `battle.lua` | `RegisterWorkflowForSkills` | 字符串拼接参与者应为 string | 已对 root proto 的 C6/C4 调用统一加搬移链，并加定向门禁，待真机 |

## 4. 操作约束（避免“拆东墙补西墙”）

1. 每次只改一种规则族（例如仅改 existing-slice 对齐判断），禁止同一提交混入多类策略改动。  
2. 改动后至少核对上表 5 个关键点，再发插件给真机。  
3. 新增规则必须写“命中形态”，避免泛化条件（例如无上限窗口、无 source 约束）。  
4. 真机出现新错误时，先回写本文件“当前主线问题”，再继续改代码。  
5. 若连续 2 次提交都引入新主线回归，下一次必须先做“规则收窄/回退”而非继续加新规则。  

## 5. 本轮后续计划（进行中）

1. 真机回归 `battle.lua/RegisterWorkflowForSkills`（优先验证当前主线）。  
2. 若仍报错，记录首条栈并补充“对应 proto/pc”后再改规则。  
3. battle 通过后再扩展到其它模块回归。  

## 6. 错误指纹台账（真机）

| 日期 | 构建/Commit | 入口 | 首条报错指纹 | 归类 | 处理状态 |
|---|---|---|---|---|---|
| 2026-03-22 | `待提交` | `jygame/battle.lua` | `RegisterWorkflowForSkills: attempt to concatenate a table value`（仍复现） | call6 规则命中过宽（函数体链式调用被误改） | 已收窄到 root proto 并重编 so，待真机 |
| 2026-03-22 | `cd134e7` | `jygame/battle.lua` | `RegisterWorkflowForSkills: attempt to concatenate a table value` | 长链 `CALL(C=6)` 参数重排残留 | 已加 force copy-fallback，待真机 |
| 2026-03-21 | `5a9ceaa` | `jygame/battle.lua` | `RegisterWorkflowForSkills: attempt to concatenate a table value` | FR2 参数错位（KSTR+TDUP 邻位） | 本地反汇编已修，待真机 |
| 2026-03-21 | `5a9ceaa` | `jygame/migong.lua` | `PatchMigong: bad argument #1 to 'insert' (table expected, got string)` | table-style 两参调用错位 | 本地反汇编已修，待真机 |

## 7. 提交前检查清单（必须）

1. 执行：`powershell -ExecutionPolicy Bypass -File .\tools\check_arm64_fr2_log.ps1`。  
2. 执行：`powershell -ExecutionPolicy Bypass -File .\tools\run_arm64_offline_regression.ps1`。  
3. 若输出失败，先补日志或修规则再提交，不允许跳过。  
4、 必须确保已报告的错误被解决或减少，不允许没有实际进步提交真机测试。
5. 每条“代码规则变更”必须带三项证据：命中形态、离线日志指纹、真机结论。  
6. 真机出现新错误时，只记录首条报错指纹，不做“批量猜测式修复”。  

## 8. 记录模板（复制一行到第 2 节）

| YYYY-MM-DD | `commit/待提交` | 规则改动一句话 + 命中形态 | 对应报错指纹 | 反汇编/真机结论 |
