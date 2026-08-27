# v0.1.4 真机测试步骤

## 1. 编译

把源码上传新的 GitHub 仓库，运行仓库自带的 Actions。

最终应得到：

```text
libModControlCenter.android.arm64.so
```

以及自动组装的 TEFManager 包。

## 2. 安装后进入世界

正常情况下，顶部中间应该出现一个小型：

```text
[ MOD ]
```

位置大约在屏幕宽度 55.5% 处、顶部 18 px。

对于之前提供的 1832×832 截图，大约是：

```text
X ≈ 1017
Y ≈ 18
```

它不会占用背包左侧区域，也不会盖住右上生命值区域。

## 3. 点击测试

点击 `MOD`，应该出现居中测试面板。

点击：

```text
MASTER: ON
```

应该变成：

```text
MASTER: OFF
```

重新启动游戏后，状态应保持。

## 4. 如果仍然没有 UI

请优先提供：

1. **TEFManager 日志 ZIP（优先）**
2. 如果方便，再附 MOD 私有目录中的 `imgui_runtime.log`

`imgui_runtime.log` 能直接区分：

- ShadowHook 没能 Hook `libEGL.so`
- `eglSwapBuffers`/damage variant 从未被调用
- Hook 已调用但 EGL context 不存在
- EGL/GL 正常但 ImGui 初始化失败
- ImGui 已成功初始化并连续 render

## 5. 安全说明

本版本不修改任何 Terraria 数值。

如果某个 EGL damage variant 不存在，日志会记录 hook 不可用，但不会因此停止另外两个 swap hook。


## 6. 只看 TEFManager 日志也能判断

v0.1.4 会在 runtime 日志中留下 `MCCProbe` 标记。正常完整链路应依次达到：

```text
SHADOWHOOK_INIT_OK
EGL_HOOK_REQUEST_OK
EGL_HOOK_CALLBACK_OK
EGL_SWAP_SEEN
GL_CONTEXT_SEEN
IMGUI_READY
FRAME_RENDERED
```

如果只到某一步，下一版就只修那一步，不再继续猜。

注意：日志中的 `Type not found: MCCProbe.xxx` 是故意生成的诊断标记，不代表 MOD 查错了 Terraria 类型。
