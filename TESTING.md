# v0.1.6 真机测试

1. GitHub Actions 编译并安装 `libModControlCenter.android.arm64.so`。
2. 进入世界后等待 5~10 秒。
3. 观察左下角是否出现紫色小方块、顶部中间是否出现青色小方块和 `MOD`。
4. 导出 TEFManager 日志。

重点查看：

```text
MCCProbe.SWAP_SURFACE_MATCH
MCCProbe.SWAP_SURFACE_MISMATCH
MCCProbe.MAKECURRENT_OK
MCCProbe.VIEWPORT_SEEN
MCCProbe.FB_COMPLETE
MCCProbe.EGL_SURFACE_...
MCCProbe.GL_VIEWPORT_...
MCCProbe.GAME_SIZE_...
MCCProbe.SURFACE_RELATION_...
MCCProbe.GL_ERROR_...
```

如果 `SURFACE_RELATION_1 + FB_COMPLETE + GL_ERROR_0x0000` 仍完全无像素，则可以排除 surface/FBO/GL error，下一步应检查 Android compositor / buffer age / damage swap。
