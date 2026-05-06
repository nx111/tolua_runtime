# ARM64 FR2 修复重启指引

## 1. 先看什么

1. 先看 [arm64_fr2_change_log.md](D:/Games/work/tolua_runtime/docs/arm64_fr2_change_log.md) 最新几条记录。
2. 再看当前工作区：`git status --short`。
3. 再确认当前基线：
   - 最近源码提交：`d677548`
   - 最近 build tag：`arm64fr2-20260505-restlog-bra-max-extend2-log`

## 2. 固定环境

- 仓库：`D:\Games\work\tolua_runtime`
- Unity 工程：`D:\Games\work\jygame_renew`
- MOD bytecode：`D:\Games\work\jygame_renew\gamedata\modcache\SSWS_HG\lua\luajit`
- 反编译源码：`D:\Games\work\jygame_renew\gamedata\modcache\SSWS_HG\lua\source`
- WSL 发行版：`Ubuntu-24.04`
- NDK：`D:\Mobile\sdk\linux\ndk\android-ndk-r21e`
- repo so：`D:\Games\work\tolua_runtime\Plugins\Android\libs\arm64-v8a\libtolua.so`
- Unity so：`D:\Games\work\jygame_renew\Assets\Plugins\Android\libs\arm64-v8a\libtolua.so`

## 3. 固定规则

- 运行时补丁只能按指令形态限定。
- 允许用 `ctx->proto_index` 收窄范围。
- 不要用固定 `pc`、固定 `line`、函数名作为运行时命中条件。
- 先找根因，不要补宽兜底规则。
- 每次有效修复都要追加 [arm64_fr2_change_log.md](D:/Games/work/tolua_runtime/docs/arm64_fr2_change_log.md)。
- 每次改 `tolua.c` / `tolua_fr1_to_fr2.c` / 离线工具后，都要跑离线回归。
- 不提交 `so`、`pdb`、`tmp`、`host/buildvm`、`bcrun_cli_wsl` 等生成物。
- 版本日志只保留在 `tolua_bytecode_android_ctor`。

## 4. 先信什么，不信什么

- 先信 `lua/luajit` 的当前 bytecode dump。
- `lua/source` 只用来理解语义，不用来判断寄存器布局。
- 旧的 `tmp/*.dis.txt`、旧 APK、旧 compare dump 只能做旁证，不能当主依据。
- 同一错误反复出现时，先补离线 probe，再继续改规则。

## 5. 标准工作流

1. 用用户给的第一条栈，定位 chunk 和大致语义。
2. 从 `lua/luajit` dump 当前 raw proto，确认真实窗口。
3. 用 `bcconv_cli_msvc_dbg.exe` 本地转一次，再 dump 转换后窗口，确认坏形态落点。
4. 在 [tolua_fr1_to_fr2.c](D:/Games/work/tolua_runtime/tolua_fr1_to_fr2.c) 增加窄规则。
5. 在 [tools/bcrun_cli.c](D:/Games/work/tolua_runtime/tools/bcrun_cli.c) 和 [tools/run_arm64_offline_regression.ps1](D:/Games/work/tolua_runtime/tools/run_arm64_offline_regression.ps1) 补对应 probe / gate。
6. 更新 [tolua.c](D:/Games/work/tolua_runtime/tolua.c) 的 build tag。
7. 追加 [arm64_fr2_change_log.md](D:/Games/work/tolua_runtime/docs/arm64_fr2_change_log.md)。
8. 跑离线回归，通过后再重编 so。

## 6. 常用命令

### 6.1 dump 当前 raw bytecode

```powershell
wsl -d Ubuntu-24.04 bash -lc "cd '/mnt/d/Games/work/tolua_runtime' && ./tools/bc_dump_proto_wsl '/mnt/d/Games/work/jygame_renew/gamedata/modcache/SSWS_HG/lua/luajit/battle.lua' 132 1150 1175"
```

### 6.2 本地转换后再看窗口

```powershell
tools\bcconv_cli_msvc_dbg.exe "D:\Games\work\jygame_renew\gamedata\modcache\SSWS_HG\lua\luajit\battle.lua" "tmp\battle.cur.fr2.bin" 1
tools\bc_dump_proto_msvc.exe "tmp\battle.cur.fr2.bin" 132 1150 1175
```

### 6.3 全量离线回归

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_arm64_offline_regression.ps1 -BytecodeDir "D:\Games\work\jygame_renew\gamedata\modcache\SSWS_HG\lua\luajit" -OutDir "tmp\offline_regression_manual" -WslDistro "Ubuntu-24.04"
```

### 6.4 日志校验

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_arm64_fr2_log.ps1
```

### 6.5 重编 arm64 so

```powershell
wsl -d Ubuntu-24.04 bash -lc "cd '/mnt/D/Games/work/tolua_runtime' && export NDK='/mnt/d/Mobile/sdk/linux/ndk/android-ndk-r21e' && ./build_arm64.sh"
```

### 6.6 同步到 Unity

```powershell
Copy-Item -Force "D:\Games\work\tolua_runtime\Plugins\Android\libs\arm64-v8a\libtolua.so" "D:\Games\work\jygame_renew\Assets\Plugins\Android\libs\arm64-v8a\libtolua.so"
```

### 6.7 校验哈希和版本串

```powershell
Get-FileHash "D:\Games\work\tolua_runtime\Plugins\Android\libs\arm64-v8a\libtolua.so" -Algorithm SHA256
Get-FileHash "D:\Games\work\jygame_renew\Assets\Plugins\Android\libs\arm64-v8a\libtolua.so" -Algorithm SHA256
Select-String -Path "D:\Games\work\tolua_runtime\Plugins\Android\libs\arm64-v8a\libtolua.so" -Pattern "arm64fr2-" -SimpleMatch
```

## 7. 关键文件

- 运行时入口：[tolua.c](D:/Games/work/tolua_runtime/tolua.c)
- FR1 -> FR2 核心：[tolua_fr1_to_fr2.c](D:/Games/work/tolua_runtime/tolua_fr1_to_fr2.c)
- 主离线 harness：[tools/bcrun_cli.c](D:/Games/work/tolua_runtime/tools/bcrun_cli.c)
- 主回归脚本：[tools/run_arm64_offline_regression.ps1](D:/Games/work/tolua_runtime/tools/run_arm64_offline_regression.ps1)
- 变更日志：[arm64_fr2_change_log.md](D:/Games/work/tolua_runtime/docs/arm64_fr2_change_log.md)

## 8. 提交约定

- 代码修复提交只包含源码和文档。
- `so` 不提交。
- 提交前至少执行：
  - `git diff --cached --check`
  - `powershell -ExecutionPolicy Bypass -File .\tools\check_arm64_fr2_log.ps1`
- 能跑回归时，优先附上 `run_arm64_offline_regression.ps1` 结果。

## 9. 当前已知边界

- `attacklogic_extendtalents_maxprobe` 仍是诊断型，不纳入强门禁；它会受 `AttackLogic.lua` 顶层注册中断影响。
- 如果真机继续只给 `stack traceback:` 而没有首条 Lua 行号，先要版本日志和更早一条带 `tmp.lua:行号` 的报错，裸地址栈不能直接修。
