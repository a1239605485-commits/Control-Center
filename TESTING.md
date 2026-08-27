# v0.1.1 真机测试顺序

## 1. 编译

把整个仓库上传 GitHub，运行 `Build MOD Control Center ImGui Test`。

构建过程会联网拉取官方 Dear ImGui v1.92.9。

最终动态库应为：

`libModControlCenter.android.arm64.so`

Actions 会同时输出可安装的 `ModControlCenter/` artifact。

## 2. 第一次安装

这是一个独立 MOD：

- pkgId: `celso.modcontrolcenter`
- 不依赖 Resource Saver
- 不修改任何游戏数值

建议测试时只启用 KernelLoader + ModControlCenter，先减少其它 MOD 干扰。

## 3. 成功标准

进入游戏或主菜单后应看到固定窗口：

- 标题：`MOD Control Center - UI Test`
- 大按钮：`MASTER: ON` 或 `MASTER: OFF`
- 点击后状态切换
- 重启游戏后状态仍保持

## 4. 如果 UI 没出现

到本 MOD 的 private_dir 找：

`imgui_runtime.log`

重点内容：

- `GL_VERSION=...`
- `GL_RENDERER=...`
- `imgui: initialized successfully`

如果仍然没有 UI，但游戏正常运行，则重点检查 `imgui_runtime.log`：如果 `GL_VERSION` 为空或反复显示 `no current OpenGL ES context yet`，说明 `Main.Draw` 所在线程没有当前 EGL/GL 上下文，下一版应把 ImGui 渲染移到真正的 EGL 提交阶段，而不是继续修改 ImGui 控件代码。

## 5. 如果能显示但点不了

观察窗口里的：

`Touch: x=... y=... down=yes/no`

如果坐标会变但 down 永远是 no，则下一版只需要修 Android 触控桥，不需要修改 ImGui 渲染。

## 6. 当前刻意不做

- 中文字体
- Resource Saver 接入
- 模块注册
- 滑块
- 拦截游戏底层点击

这些都等 v0.1 的 ImGui 渲染与触控稳定后再加。
