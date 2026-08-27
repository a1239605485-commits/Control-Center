# 泰拉 MOD 控制中心 v0.1.6 — EGL Surface / Viewport 修正版

本版本继续只测试 ImGui 控制中心，不修改 Terraria 游戏数值。

## 基于 v0.1.5 日志得到的结论

已确认：ShadowHook、eglSwapBuffers、GL Context、ImGui 初始化、RenderDrawData、framebuffer 0 均执行成功。
同时 `FBO_NONZERO_SEEN` 未出现，因此 v0.1.5 的问题不是离屏 FBO。

## v0.1.6 新增

1. 检查 `eglGetCurrentSurface(EGL_DRAW)` 是否与 `eglSwapBuffers()` 传入 surface 相同。
2. 若二者是不同的有效 surface，且同属当前 EGLDisplay，则临时 `eglMakeCurrent()` 到真正要 swap 的 surface，绘制完成后恢复。
3. 记录 EGL surface 尺寸、GL viewport 尺寸和 Terraria game size 到 TEFKernel 可见探针。
4. 绘制前显式设置 `glViewport(0,0,surfaceW,surfaceH)`，完成后恢复。
5. 检查默认 framebuffer 完整性和 GL error。
6. 原生探针改为两个：左下角 28x28 紫色块 + 顶部 MOD 按钮附近 18x18 青色块。

## 关键日志标记

正常会新增以下 `MCCProbe.*`：

- `CURRENT_DRAW_SURFACE_SEEN`
- `SWAP_SURFACE_MATCH` 或 `SWAP_SURFACE_MISMATCH`
- `MAKECURRENT_OK` / `MAKECURRENT_FAIL`（仅 mismatch 时可能出现）
- `VIEWPORT_SEEN`
- `FB_COMPLETE` 或 `FB_INCOMPLETE`
- `GL_ERROR_SEEN`（只有出现 GL 错误时）
- `EGL_SURFACE_<宽>x<高>`
- `GL_VIEWPORT_<x>_<y>_<宽>_<高>`
- `GAME_SIZE_<宽>x<高>`
- `SURFACE_RELATION_1/2/3`
- `GL_ERROR_0x....`

`SURFACE_RELATION`：1=当前 draw surface 与 swap surface 一致；2=不一致；3=当前没有 draw surface。

## 屏幕观察

- 左下角应出现 28x28 紫色小方块。
- 顶部中间 MOD 入口左侧应出现 18x18 青色小方块。
- ImGui 的 `MOD` 按钮仍位于顶部中间。

作者：liuxin
