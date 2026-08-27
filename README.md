# 泰拉 MOD 控制中心 v0.1.5 — ImGui Native EGL Test

作者：liuxin  
包名：`celso.modcontrolcenter`

这是“统一 MOD 控制中心”的第一阶段渲染测试版。它 **不包含 Resource Saver、Less Grind 或任何游戏数值修改**。

## 本版目标

v0.1.4 的 TEFKernel 探针已经确认整条 native 链路全部跑通：`SHADOWHOOK_INIT_OK → EGL_HOOK_REQUEST_OK → EGL_SWAP_SEEN → GL_CONTEXT_SEEN → IMGUI_READY → FRAME_RENDERED`。因此 v0.1.5 不再修改 Hook 架构，只修最后的 framebuffer 可见性问题。

Dear ImGui 的 OpenGL renderer 会绘制到“当前绑定 framebuffer”。Terraria 在 `eglSwapBuffers()` 前可能仍保留离屏 FBO，导致 ImGui 实际渲染成功但像素留在离屏纹理中。v0.1.5 会在绘制前保存当前 FBO，临时绑定默认 framebuffer 0，再绘制 ImGui，完成后恢复原 FBO。

仍直接拦截 Android 最终 EGL 提交函数：

- `eglSwapBuffers`
- `eglSwapBuffersWithDamageKHR`
- `eglSwapBuffersWithDamageEXT`

使用 **ByteDance ShadowHook v1.0.10** 在 `libEGL.so` 上做 arm64 inline hook。

之所以固定到 v1.0.10，是因为 KernelLoader 的 MOD 需要单 `.so` 自包含；ShadowHook 新版引入了额外的 `libshadowhook_nothing.so`/linker 初始化依赖，不适合我们之前那种直接静态塞入一个 MOD `.so` 的方式。v1.0.10 仍支持 Android 15 和 16 KB page size，更适合这一轮单文件验证。

渲染链：

```text
Terraria / MonoGame 绘制完成
        ↓
Android EGL
        ↓
eglSwapBuffers / Damage variant
        ↓
【ModControlCenter】
保存当前 framebuffer
绑定 framebuffer 0
        ↓
ImGui::NewFrame
绘制 MOD 按钮 / 测试面板
ImGui::Render
        ↓
恢复原 framebuffer
        ↓
调用原始 EGL swap
        ↓
屏幕
```

## UI 位置

平时只显示一个很小的 `MOD` 按钮，不再显示 620×280 的大面板。

按钮位置根据 1832×832 真机截图调整：

```text
X ≈ 屏幕宽度 × 55.5%
Y = 18 px
尺寸约 124 × 58 px
```

也就是顶部中间、背包/快捷栏与右侧生命/装备 UI 之间的空白区域。

点击 `MOD` 后才弹出居中的测试面板，里面只有：

```text
MASTER: ON / OFF
```

用于验证 Android 触控和配置保存。

## 输入设计

EGL render thread **不会调用 TEFKernel patchlib**。

`Main.Update(GameTime)` Postfix 只负责在游戏线程采样：

- `Main.mouseX`
- `Main.mouseY`
- `Main.mouseLeft`
- `Main.screenWidth`
- `Main.screenHeight`

然后用 `std::atomic` 把快照交给 EGL hook。这能避免在 native EGL 线程跨线程访问 IL2CPP/TEFKernel 对象。

## 配置

第一次运行会在 MOD 私有目录创建：

```text
control_center.ini
```

内容：

```ini
master=1
```

点击测试总开关后立即保存。

## 诊断日志

私有目录还会生成：

```text
imgui_runtime.log
```

成功时重点应看到：

```text
shadowhook: initialized in SHARED mode (v1.0.10 static path)
shadowhook callback: err=0 lib=libEGL.so sym=eglSwapBuffers ...
egl: first native swap intercepted via eglSwapBuffers
EGL surface=1832x832
GL_VERSION=...
GL_RENDERER=...
imgui: initialized successfully inside eglSwapBuffers
```

如果游戏使用 damage swap，则第一条 native swap 可能显示：

```text
eglSwapBuffersWithDamageKHR
```

或：

```text
eglSwapBuffersWithDamageEXT
```

## 第三方依赖

构建时由 CMake 自动获取：

- Dear ImGui v1.92.9 — MIT License
- ByteDance ShadowHook v1.0.10 — MIT License

ShadowHook v1.0.10 源码静态编入 `libModControlCenter.android.arm64.so`，不会要求用户额外安装第二个 `.so`。

## TEFKernel 可见探针

因为 TEFManager 导出的日志不会包含 MOD 私有目录里的 `imgui_runtime.log`，v0.1.5 另外增加了一套 **TEFKernel 可见探针**。它会故意查询不存在的 `MCCProbe.*` 类型，因此 runtime 日志会出现 `Type not found`，但这只是诊断标记，不是错误。

可能看到：

```text
MCCProbe.SHADOWHOOK_INIT_OK_IMMEDIATE
MCCProbe.EGL_HOOK_REQUEST_OK_IMMEDIATE
MCCProbe.EGL_HOOK_CALLBACK_OK
MCCProbe.EGL_SWAP_SEEN
MCCProbe.GL_CONTEXT_SEEN
MCCProbe.IMGUI_READY
MCCProbe.FRAME_RENDERED
```

v0.1.5 还新增：

```text
MCCProbe.FBO_NONZERO_SEEN
MCCProbe.FBO_ZERO_BOUND
```

如果出现 `FBO_NONZERO_SEEN`，就直接证明 Terraria 在 swap 前仍绑定着离屏 framebuffer；`FBO_ZERO_BOUND` 表示 MOD 已经成功切到默认 framebuffer 0。

此外，`MOD` 按钮左侧会绘制一个约 14×14 px 的紫色原生 GLES 小方块。它不经过 ImGui，用来单独验证 framebuffer 0 是否真正显示在屏幕上。
