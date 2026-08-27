# v0.1.2 真机测试

1. 用 GitHub Actions 编译 arm64。
2. 安装/替换 `libModControlCenter.android.arm64.so`。
3. 进入世界后观察屏幕左上：X≈24、Y≈90。
4. 正常应出现 `MOD Control Center - Present Test`。
5. 点击 `MASTER: ON/OFF`，重启游戏确认状态保持。

如果 UI 不出现，请提供：

- TEFManager 最新日志 ZIP；
- 如果方便，再提供 MOD 私有目录中的 `imgui_runtime.log`。

`imgui_runtime.log` 最关键的判断：

- 有 `Present prefix hook id=`：说明渲染挂点安装成功。
- 有 `GL_VERSION=`：说明 Present 回调处存在当前 GL Context。
- 有 `initialized successfully on Present thread`：说明 ImGui renderer 已真正初始化。
- 如果持续是 `Present hook ran without a current GL context`，下一步才需要转到 native `eglSwapBuffers` hook。
