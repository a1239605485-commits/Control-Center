# 泰拉 MOD 控制中心 v0.1.3 — ImGui Native EGL Test

作者：liuxin  
包名：`celso.modcontrolcenter`

这是“统一 MOD 控制中心”的第一阶段渲染测试版。它 **不包含 Resource Saver、Less Grind 或任何游戏数值修改**。

## 本版目标

前两版已经确认 KernelLoader MOD 可以加载、`Main.Draw` / `GraphicsDevice.Present` 也能 Hook，但 ImGui 没有真正出现在最终画面里。因此 v0.1.3 停止在 MonoGame 管理层提交 UI，改为直接拦截 Android 最终 EGL 提交函数：

- `eglSwapBuffers`
- `eglSwapBuffersWithDamageKHR`
- `eglSwapBuffersWithDamageEXT`

使用 ByteDance ShadowHook v2.0.1 在 `libEGL.so` 上做 arm64 inline hook。

渲染链：

```text
Terraria / MonoGame 绘制完成
        ↓
Android EGL
        ↓
eglSwapBuffers / Damage variant
        ↓
【ModControlCenter】
ImGui::NewFrame
绘制 MOD 按钮 / 测试面板
ImGui::Render
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
shadowhook: initialized in MULTI mode
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
- ByteDance ShadowHook v2.0.1 — MIT License

ShadowHook 源码静态编入 `libModControlCenter.android.arm64.so`，不会要求用户额外安装第二个 `.so`。
