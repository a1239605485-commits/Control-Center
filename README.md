# 泰拉 MOD 控制中心 - ImGui Test v0.1.0

这是独立 UI 基础设施测试版，不包含 Resource Saver、Less Grind 或任何游戏数值修改。

## 本版只验证

1. `Terraria.Main.Draw(GameTime)` Postfix 能否安全运行原生 OpenGL ES 绘制。
2. Dear ImGui v1.92.9 能否在 Terraria Android 的当前 GLES3 context 上初始化。
3. Terraria `Main.mouseX / mouseY / mouseLeft` 能否驱动 ImGui 按钮。
4. `Master` 总开关能否写入 MOD `private_dir/control_center.ini` 并在下次启动恢复。

## 安全设计

- 不调用 Terraria/MonoGame `SpriteBatch.Begin/End/DrawString`。
- 不调用任何 IL2CPP `Vector2/Color` 结构体绘图接口。
- 不使用 `imgui_impl_android`，避免接管 Android 输入链。
- 只使用 Dear ImGui 核心 + 官方 `imgui_impl_opengl3` renderer backend。
- 输入由 Terraria 已有 `mouseX/mouseY/mouseLeft` 喂给 ImGuiIO。
- 如果 `Main.Draw` Postfix 时没有 OpenGL ES context，或检测不到 OpenGL ES 3，则 UI 安全禁用，不应影响游戏。

## Dear ImGui

CMake 构建时通过 FetchContent 拉取并固定到 `v1.92.9`，不在此源码包中复制第三方源文件。

## 运行后诊断

MOD 私有目录会生成：

- `control_center.ini`：测试总开关状态。
- `imgui_runtime.log`：GL 版本、renderer、ImGui 初始化结果。

如果 UI 没出现，请将 TEFManager 日志和 `imgui_runtime.log` 一起提供。

## UI

第一版故意使用英文 ASCII 字体，避免中文字体问题干扰 ImGui 渲染链验证。确认 UI 稳定后，下一版再接入中文字体和模块注册系统。
