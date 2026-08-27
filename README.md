# 泰拉 MOD 控制中心 / Terraria MOD Control Center v0.1.2

这是一个**纯 UI 测试 MOD**，不修改 Terraria 的任何游戏数值。

## v0.1.2 的唯一目标

把 Dear ImGui 的提交点从 `Terraria.Main.Draw(GameTime)` 移到 MonoGame 的：

- 首选：`Microsoft.Xna.Framework.Graphics.GraphicsDevice.Present()` Prefix
- 回退：`Microsoft.Xna.Framework.GraphicsDeviceManager.EndDraw()` Prefix

MonoGame 的 `EndDraw()` 最终调用 `GraphicsDevice.Present()`；而 `Present()` 内部再进入平台 `PlatformPresent()`。因此 Prefix 位于“游戏画面已完成、最终 swap 之前”。

## 测试 UI 位置

固定显示在屏幕左上区域：

- X = 24 px
- Y = 90 px
- 宽度 = `min(620 px, 屏幕宽度 × 62%)`
- 高度 = 280 px

标题：`MOD Control Center - Present Test`

只有一个按钮：`MASTER: ON/OFF`。

## 诊断文件

MOD 私有目录中：

`imgui_runtime.log`

成功时重点看：

```text
init: GraphicsDevice.Present prefix hook id=...
GL_VERSION=...
GL_RENDERER=...
imgui: initialized successfully on Present thread
```

如果 `Present` 不存在，会自动尝试 `GraphicsDeviceManager.EndDraw()`。
