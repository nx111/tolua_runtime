# ARM64 加载 ARM32 Bytecode 变更日志（防回归）

最后更新：2026-03-21  
维护规则：每次改 `tolua.c` 或重编插件后，必须追加一条记录并更新回归矩阵。

## 1. 当前目标

- 目标：在 `arm64` 上稳定加载 `arm32` LuaJIT bytecode。
- 当前主线问题：
1. `jygame/battle.lua`：`RegisterWorkflowForSkills` 报 `attempt to concatenate a table value`。
2. `jygame/migong.lua`：`PatchMigong` 报 `bad argument #1 to 'insert' (table expected, got string)`。

## 2. 变更记录（按时间倒序）

| 日期 | Commit | 变更摘要 | 目标问题 | 当前结论 |
|---|---|---|---|---|
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
| `migong.lua` | `PatchMigong` 内 `table.insert` | 第1参数应为 table | **失败（当前主线）** |
| `battle.lua` | `CheckIfEquipNotRight` 调用 | 参数顺序正确 | 通过（已修） |
| `battle.lua` | `RegisterWorkflowForSkills` | 字符串拼接参与者应为 string | **失败（当前主线）** |

## 4. 操作约束（避免“拆东墙补西墙”）

1. 每次只改一种规则族（例如仅改 existing-slice 对齐判断），禁止同一提交混入多类策略改动。  
2. 改动后至少核对上表 5 个关键点，再发插件给真机。  
3. 新增规则必须写“命中形态”，避免泛化条件（例如无上限窗口、无 source 约束）。  
4. 真机出现新错误时，先回写本文件“当前主线问题”，再继续改代码。  
5. 若连续 2 次提交都引入新主线回归，下一次必须先做“规则收窄/回退”而非继续加新规则。  

## 5. 本轮后续计划（进行中）

1. 精确定位 `PatchMigong` 报错点对应的 CALL 参数布局，新增“仅该形态”修正。  
2. 精确定位 `RegisterWorkflowForSkills` 的 `CAT`/`CALL` 邻近寄存器写入错位，新增“仅该形态”修正。  
3. 完成后重编 `arm64-v8a`，更新本日志与回归矩阵。  

