# LuaJIT 同步

当前 `luajit-2.1` 按“上游快照 + 本地 patch”维护。

上游源：
- GitHub: `https://github.com/LuaJIT/LuaJIT`
- 默认分支/引用：`v2.1`

同步命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_luajit_upstream.ps1
```

脚本会做 4 件事：

1. 从 GitHub 拉取/更新 LuaJIT 缓存仓库。
2. 用 `git archive` 导出快照到 `luajit-2.1`。
3. 重新应用 `tools/luajit-upstream-patches/*.patch`。
4. 重写 `luajit-2.1/.upstream-sync`，记录本次同步的 `url/ref/commit/relver`。

为什么不用直接拷 clone 工作区：

- 上游 rolling 版本号依赖 `.relver`。
- 直接拷工作区时，`.relver` 还是占位符，不是真实提交时间。
- `git archive` 会把它导成可用的实际值，构建时版本号才稳定。

当前保留的本地 patch 只允许两类：

- 项目兼容性改动，例如 `LUA_IDSIZE=128`。
- 旧 uLua bytecode 兼容和 vendored `.relver` 读取规则。

新增本地 patch 时，先改 `luajit-2.1`，再更新 `tools/luajit-upstream-patches/`，不要直接只改 vendor 目录不留 patch。
