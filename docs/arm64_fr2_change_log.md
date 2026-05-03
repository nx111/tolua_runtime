# ARM64 加载 ARM32 Bytecode 变更日志（防回归）

最后更新：2026-03-27 17:05  
维护规则：每次改 `tolua.c` 或重编插件后，必须追加一条记录并更新回归矩阵；提交前必须执行 `tools/check_arm64_fr2_log.ps1`。

## 1. 当前目标

- 目标：在 `arm64` 上稳定加载 `arm32` LuaJIT bytecode。
- 当前主线问题：
1. `jygame/battle.lua`：`tmp.lua:2556 field or property Equipment does not exist`、`tmp.lua:2571 invalid arguments to method GetEquipment`（定位为 `CALL(C=2/3/4)` 在 FR2 下参数窗口右移导致 self/arg 错位）。

## 2. 变更记录（按时间倒序）

| 日期 | Commit | 变更摘要 | 目标问题 | 当前结论 |
|---|---|---|---|---|
| 2026-05-03 | `待提交` | 移除 `TGET*(tbl!=base)+TDUP+CALL(C=2)` 的跳过规则；该形态在 FR2 必须把 `TDUP` 从 `CALL A+1` 移到 `CALL A+2` | `jygame/battle.lua tmp.lua:64 bad argument #1 to ipairs`，`tmp.lua:169 List2Map` 仍复现；根因是上轮把 `TDUP A20 + CALL A19` 误判为正确 FR2 形态 | 离线解密确认 `lua/battle.lua` 解密结果与 `lua/luajit/battle.lua` SHA256 一致；转换后 `proto202 line169/241` 为 `TGETS A19; TDUP A21; CALL A19 C2`；`tools/check_fr1_callc2_tdup.py` 已改为校验 FR2 产物 `TDUP == CALL A+2`；MSVC 离线转换 `main/battle/migong` 均 `converted=1 failed=0`；arm64 so 已重编并同步到 Unity，build tag `arm64fr2-20260503-tdup-callc2-shift`，so 不提交 |
| 2026-05-03 | `481b37c` | 新增 `tools/check_fr1_callc2_tdup.py` 离线门禁，直接解析 SSWS_HG FR1 bytecode，检查 `TGET* + TDUP + CALL(C=2)` 形态；用 NDK r21e 本地重编/同步 arm64 `libtolua.so`，但 so 不纳入提交 | `jygame/battle.lua tmp.lua:64 bad argument #1 to ipairs`，`tmp.lua:169 List2Map` 首报错 | 结论已被本轮推翻：FR2 一参调用的正确实参槽是 `CALL A+2`，保留 `TDUP A20` 会让 `List2Map` 收到旧槽字符串 |
| 2026-03-27 | `待提交` | `CALL(C=2)` 改为“main+battle 混合窄规则”：保留 `e8660fb` passthrough 主路径；补回 `mirrored MOV` 与 `TGET*(tbl!=base)+TDUP` 两个定向跳过 | 同时满足 `main.lua line10 nil` 不回归 + `battle line137/169/241` 不错位 | 对照确认：`main.lua` 转换产物继续与 `e8660fb` MD5 一致（`d3405e575caac40eff41d2dbb6009c70`）；`battle proto8 line137` 恢复 `MOV A4<-A0`，`proto202 line169/241` 恢复 `TDUP A20`；离线回归通过 |
| 2026-03-27 | `待提交` | 回退 `CALL(C=2)` 到 `e8660fb` 的窄条件（仅 `MOV arg1<-passthrough + TGET*/UGET/GGET + CALL(C=2)` 跳过右移），移除 broad/镜像/`TDUP` 跳过 | `DoFile failed: jygame/main.lua err: tmp.lua:10: attempt to index a nil value` | 对照同源 `main.lua`：当前转换产物与 `e8660fb` MD5 完全一致（`d3405e575caac40eff41d2dbb6009c70`）；离线回归 `main/battle/migong` 全部 `conversion_failed=0` |
| 2026-03-27 | `待提交` | 将 `direct CALL(C=2)` 收敛为统一跳过 FR2 参数右移（覆盖 MOV/TDUP 等一参直调形态） | `tmp.lua:64 bad argument #1 to ipairs (table expected, got string)` 与 `line169 List2Map` 参数错位反复 | 离线回归 `offline_regression_semantic11` 通过；`proto202 line169/241` 均保持 `TDUP A20 + CALL A19 C2` |
| 2026-03-27 | `待提交` | `CALL(C=2)` 规则补充 `TDUP` 实参布局：支持 `func(TGET/UGET/GGET) + TDUP(arg) + CALL` 跳过右移，修复 `line169` 传入 `List2Map` 的参数错位 | `tmp.lua:64 bad argument #1 to ipairs (table expected, got string)` | 离线回归 `offline_regression_semantic10` 通过；`proto202 line169` 恢复 `TDUP A20`（不再被改为 `A21`） |
| 2026-03-27 | `待提交` | `CALL(C=2)` passthrough 规则补充镜像布局：支持 `func(TGET/UGET/GGET) + MOV(arg) + CALL`，避免 `line137 CreateLuaTable` 参数落到 `A+2` | `BATTLE_BeforeRoleAction tmp.lua:137 invalid arguments to CreateLuaTable` | 离线回归 `offline_regression_semantic9` 通过；`proto8 pc3` 命中 `skip FR2 arg shift for direct CALL(C=2)`，反汇编确认 `MOV A4<-A0` 恢复 |
| 2026-03-27 | `待提交` | 在 `tolua_shift_proto_slice_right_for_fr2` 的 live-after copy-insert 分支新增 `CALL(B=1,C>=4)` 整帧右移（含函数寄存器 `A`）并同步 `A+=1` | `BATTLE_BeforeInitBattle tmp.lua:711 bad argument #1 to insert (table expected, got string)` | 离线回归 `offline_regression_semantic8` 通过；`battle proto19 pc705/729` 由“仅参数右移”升级为“整帧右移 + `CALL A9->A10`”；关键指纹保持 `workflow_shift_hits=21` |
| 2026-03-27 | `待提交` | 在 `tolua_try_insert_copy_fallback_for_fr2` 的 live-after 分支补齐 `CALL(B=1,C>=4)` 保护：优先 copy-insert（禁用 spill 重写） | `BATTLE_BeforeInitBattle tmp.lua:711 bad argument #1 to insert (table expected, got string)` | 离线回归 `offline_regression_semantic6` 通过（`main/battle/migong conversion_failed=0`）；battle 日志仍保持 `proto19 pc704/727 live-after fallback copy insert`，未回退为 spill |
| 2026-03-27 | `待提交` | 禁用 `CALL(B=1,C>=4)` 的 `live-after spill fallback`，改用纯 copy-insert 右移（不做 spill 重写） | `BATTLE_BeforeInitBattle tmp.lua:711 bad argument #1 to insert (table expected, got string)` | 离线日志 `proto19 pc704/727` 由 `live-after spill fallback` 变为 `live-after fallback copy insert`；对应调用窗口不再注入 spill 寄存器链（移除 `MOV A26/A27`），改为纯 `MOV13<-12; MOV12<-11; MOV11<-10` |
| 2026-03-27 | `待提交` | 移除 `CALL(C=3,B=1)` 与 `CALL(C=4,B=1)` 的“param-pass 直接跳过”规则，统一恢复 FR2 参数右移 | `BATTLE_BeforeInitBattle tmp.lua:2556 field or property Equipment does not exist` | 离线反汇编确认 `proto131 line2705/2707` 从 `MOV A4/A5(/A6)` 调整为 `MOV A5/A6(/A7)`；`proto19 line528` 保持 `MOV A13/A14`；`main/migong` 关键点不回退 |
| 2026-03-27 | `待提交` | 收窄 `direct CALL(C=3)` 跳过条件：从“仅 arg1 透传”改为“arg1 + arg2 均为透传 seed 才跳过”；其余场景恢复 FR2 右移 | `BATTLE_BeforeRoleAction tmp.lua:528 bad argument #1 to GetEquipment (Role expected, got number)` | 离线反汇编确认 `proto19 line528` 由 `MOV12/MOV13` 调整为 `MOV13/MOV14`；`proto130 line2571` 同步调整为 `MOV11/MOV12`；`proto131 pc234/240` 保持不变 |
| 2026-03-26 | `待提交` | 收敛 `method-self CALL(C=3)`：由“非 root proto”改为“self/base 回溯到调用者透传 seed 才跳过”；其余 method-self 正常做 FR2 右移 | `migong.lua tmp.lua:29 bad argument #2 to 'byte' (number expected, got table)` | 离线反汇编确认 `proto2 line29` 已从 `MOV16/MOV17` 调整为 `MOV17/MOV18`（参数窗口右移）；battle 关键点 `proto130 pc27/42/107/112` 与 `proto131 pc234/240` 仍保持 |
| 2026-03-26 | `待提交` | 将 `direct CALL(C=2/3)` 跳过条件从 `root proto(pflags=0x03)` 特判改为数据流判定：仅当 `arg1` 源寄存器在调用前“无最近写入”（passthrough seed）才跳过右移 | 去除临时 chunk 特判，定位真正差异 | 离线回归通过；`main proto5 pc111` 维持 FR2 正确形态（`MOV 18<-13`,`MOV 19<-15`,`CALL A16 C3`）；`battle proto130 pc42` 与 `proto131 pc234/240` 关键点保持 |
| 2026-03-26 | `待提交` | 收窄 `direct CALL(C=2/3)`：仅非 root proto(`pflags!=0x03`) 生效；root proto 回退到默认搬移，避免反射链方法调用 self/arg 错槽 | `main.lua tmp.lua:37 bad argument #1 to GetFields (Type expected, got number)` | 静态反汇编已确认 `main proto5 line37` 由 `MOV17/MOV18` 修正为 `MOV18/MOV19`（FR2 正确参数槽）；`line28 ContainsKey` 保持正确；battle 关键点未回退 |
| 2026-03-26 | `待提交` | 收窄 `method-self CALL(C=3)`：仅非 root proto(`pflags!=0x03`) 且 `arg2` 为 `MOV` 时跳过右移；保留 `CALL(C=4,B=1) param-pass` 与 `direct CALL(C=2/3)` 规则 | `main.lua tmp.lua:28 bad argument #1 to ContainsKey (Dictionary expected, got string)` | 静态反汇编已确认 `main proto5 line28` 由 `MOV14/KSTR15` 修正为 `MOV15/KSTR16`（FR2 正确参数槽）；battle 关键点 `proto130(42)/proto131(234,240)` 仍保持正确 |
| 2026-03-26 | `待提交` | 新增 4 条窄规则：`method-self CALL(C=3)`、`direct CALL(C=2)`、`direct CALL(C=3)`、`CALL(C=4,B=1) param-pass` 均跳过 FR2 参数右移，避免 `MOV` 链被整体 +1 后与 `CALL A` 脱节 | `battle.lua` 中 `CheckIfEquipNotRight/GetEquipment/RegisterPrevRole` 相关参数错位 | 静态反汇编已确认 `proto130(pc27/42/124/127)` 与 `proto131(pc224/234/240)` 参数寄存器恢复正确；离线回归通过（`main/battle/migong` 全部 `conversion_failed=0`） |
| 2026-03-22 | `待提交` | 新增 `CALL(C=3)` 窄规则：命中 `func + MOV(arg1) + TGET*(arg2) + CALL` 形态时强制 `copy-fallback`，避免 existing-slice 误保留 FR1 参数布局 | `battle.lua tmp.lua:148 slevels=nil`（`CheckIfSkillUpgraded`） | 离线已验证 `proto132` 两个调用点新增 `MOV11<-10` 与 `MOV10<-9` 搬移链；门禁通过，待真机 |
| 2026-03-22 | `待提交` | `RegisterWorkflowForSkills` 规则扩展：`CALL(C=6/C=4)` 的第二参数来源由 `TDUP` 扩展为 `TDUP|MOV`（root proto），覆盖 `AttackLogic.lua` 中 `KSTR+MOV+FNEW+...` 形态 | `AttackLogic.lua` 报 `RegisterWorkflowForSkills: attempt to concatenate a table value` | 离线反汇编已确认目标段 33 个调用点全部带搬移链（`fail=0`），待真机 |
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
| 2026-03-22 | `待提交` | `jygame/AttackLogic.lua` | `RegisterWorkflowForSkills: attempt to concatenate a table value` | call6 规则漏匹配 `MOV` 作为第二参数来源 | 已扩展 `TDUP|MOV` 并重编 so，待真机 |
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
