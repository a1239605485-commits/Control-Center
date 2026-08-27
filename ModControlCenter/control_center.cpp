#include "control_center.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "shadowhook.h"

#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/type.h"

namespace {

using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
using EglSwapBuffersWithDamageFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint*, EGLint);

patch_hook_id_t g_input_hook = PATCH_HOOK_INVALID_ID;
patch_handle_t g_field_screen_width = PATCH_NULL;
patch_handle_t g_field_screen_height = PATCH_NULL;
patch_handle_t g_field_mouse_x = PATCH_NULL;
patch_handle_t g_field_mouse_y = PATCH_NULL;
patch_handle_t g_field_mouse_left = PATCH_NULL;

void* g_swap_hook_stub = nullptr;
void* g_swap_damage_khr_hook_stub = nullptr;
void* g_swap_damage_ext_hook_stub = nullptr;
EglSwapBuffersFn g_original_egl_swap_buffers = nullptr;
EglSwapBuffersWithDamageFn g_original_egl_swap_buffers_with_damage_khr = nullptr;
EglSwapBuffersWithDamageFn g_original_egl_swap_buffers_with_damage_ext = nullptr;
thread_local bool g_inside_swap_proxy = false;

std::atomic<int> g_game_width{0};
std::atomic<int> g_game_height{0};
std::atomic<int> g_mouse_x{0};
std::atomic<int> g_mouse_y{0};
std::atomic<bool> g_mouse_left{false};
std::atomic<unsigned long long> g_swap_count{0};
std::atomic<unsigned long long> g_render_count{0};
std::atomic<unsigned int> g_native_probe_flags{0};
std::atomic<unsigned int> g_native_probe_emitted{0};
std::atomic<int> g_diag_surface_w{0};
std::atomic<int> g_diag_surface_h{0};
std::atomic<int> g_diag_viewport_x{0};
std::atomic<int> g_diag_viewport_y{0};
std::atomic<int> g_diag_viewport_w{0};
std::atomic<int> g_diag_viewport_h{0};
std::atomic<int> g_diag_surface_relation{0}; // 1=match, 2=mismatch, 3=no current draw surface
std::atomic<int> g_diag_gl_error{0};
std::atomic<bool> g_diag_dimensions_emitted{false};

enum NativeProbeFlag : unsigned int {
    PROBE_SHADOWHOOK_INIT_OK = 1u << 0,
    PROBE_EGL_HOOK_REQUEST_OK = 1u << 1,
    PROBE_EGL_HOOK_CALLBACK_OK = 1u << 2,
    PROBE_EGL_SWAP_SEEN = 1u << 3,
    PROBE_GL_CONTEXT_SEEN = 1u << 4,
    PROBE_IMGUI_READY = 1u << 5,
    PROBE_FRAME_RENDERED = 1u << 6,
    PROBE_SHADOWHOOK_INIT_FAIL = 1u << 7,
    PROBE_EGL_HOOK_REQUEST_FAIL = 1u << 8,
    PROBE_EGL_HOOK_CALLBACK_FAIL = 1u << 9,
    PROBE_FBO_NONZERO_SEEN = 1u << 10,
    PROBE_FBO_ZERO_BOUND = 1u << 11,
    PROBE_CURRENT_DRAW_SURFACE_SEEN = 1u << 12,
    PROBE_SWAP_SURFACE_MATCH = 1u << 13,
    PROBE_SWAP_SURFACE_MISMATCH = 1u << 14,
    PROBE_MAKECURRENT_OK = 1u << 15,
    PROBE_MAKECURRENT_FAIL = 1u << 16,
    PROBE_VIEWPORT_SEEN = 1u << 17,
    PROBE_FB_COMPLETE = 1u << 18,
    PROBE_FB_INCOMPLETE = 1u << 19,
    PROBE_GL_ERROR_SEEN = 1u << 20
};

bool g_imgui_ready = false;
bool g_panel_open = false;
bool g_master_enabled = true;
bool g_prev_mouse_left = false;
int g_context_miss_count = 0;

std::string g_private_dir;
std::string g_config_path;
std::string g_runtime_log_path;
std::mutex g_log_mutex;
std::chrono::steady_clock::time_point g_last_frame_time;

std::string join_path(const std::string& dir, const char* name) {
    if (dir.empty()) return std::string(name ? name : "");
    if (dir.back() == '/' || dir.back() == '\\') return dir + (name ? name : "");
    return dir + "/" + (name ? name : "");
}

void append_runtime_log(const char* text) {
    if (g_runtime_log_path.empty() || !text) return;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    FILE* f = std::fopen(g_runtime_log_path.c_str(), "a");
    if (!f) return;
    std::fprintf(f, "%s\n", text);
    std::fclose(f);
}

void append_runtime_logf(const char* fmt, ...) {
    if (g_runtime_log_path.empty() || !fmt) return;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    FILE* f = std::fopen(g_runtime_log_path.c_str(), "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

void save_config() {
    if (g_config_path.empty()) return;
    FILE* f = std::fopen(g_config_path.c_str(), "w");
    if (!f) {
        append_runtime_log("config: save failed");
        return;
    }
    std::fprintf(f, "master=%d\n", g_master_enabled ? 1 : 0);
    std::fclose(f);
}

void load_config() {
    g_master_enabled = true;
    if (g_config_path.empty()) return;

    FILE* f = std::fopen(g_config_path.c_str(), "r");
    if (!f) {
        save_config();
        return;
    }

    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        int value = 0;
        if (std::sscanf(line, "master=%d", &value) == 1) g_master_enabled = value != 0;
    }
    std::fclose(f);
}

void emit_tef_probe_marker(const char* marker) {
    if (!marker || !*marker) return;
    // Intentionally query a non-existent type. TEFKernel itself records the
    // lookup result in runtime_*.log, giving us a diagnostic channel that is
    // visible in TEFManager exports even when this MOD's private log isn't.
    patch_handle_t probe = patchlib_type_get_type("MCCProbe", marker);
    if (probe) patchlib_free(probe);
}

void flush_native_probe_markers_on_game_thread() {
    const unsigned int flags = g_native_probe_flags.load(std::memory_order_acquire);
    unsigned int emitted = g_native_probe_emitted.load(std::memory_order_relaxed);

    struct Marker { unsigned int bit; const char* name; };
    static const Marker markers[] = {
        {PROBE_SHADOWHOOK_INIT_OK, "SHADOWHOOK_INIT_OK"},
        {PROBE_SHADOWHOOK_INIT_FAIL, "SHADOWHOOK_INIT_FAIL"},
        {PROBE_EGL_HOOK_REQUEST_OK, "EGL_HOOK_REQUEST_OK"},
        {PROBE_EGL_HOOK_REQUEST_FAIL, "EGL_HOOK_REQUEST_FAIL"},
        {PROBE_EGL_HOOK_CALLBACK_OK, "EGL_HOOK_CALLBACK_OK"},
        {PROBE_EGL_HOOK_CALLBACK_FAIL, "EGL_HOOK_CALLBACK_FAIL"},
        {PROBE_EGL_SWAP_SEEN, "EGL_SWAP_SEEN"},
        {PROBE_GL_CONTEXT_SEEN, "GL_CONTEXT_SEEN"},
        {PROBE_IMGUI_READY, "IMGUI_READY"},
        {PROBE_FRAME_RENDERED, "FRAME_RENDERED"},
        {PROBE_FBO_NONZERO_SEEN, "FBO_NONZERO_SEEN"},
        {PROBE_FBO_ZERO_BOUND, "FBO_ZERO_BOUND"},
        {PROBE_CURRENT_DRAW_SURFACE_SEEN, "CURRENT_DRAW_SURFACE_SEEN"},
        {PROBE_SWAP_SURFACE_MATCH, "SWAP_SURFACE_MATCH"},
        {PROBE_SWAP_SURFACE_MISMATCH, "SWAP_SURFACE_MISMATCH"},
        {PROBE_MAKECURRENT_OK, "MAKECURRENT_OK"},
        {PROBE_MAKECURRENT_FAIL, "MAKECURRENT_FAIL"},
        {PROBE_VIEWPORT_SEEN, "VIEWPORT_SEEN"},
        {PROBE_FB_COMPLETE, "FB_COMPLETE"},
        {PROBE_FB_INCOMPLETE, "FB_INCOMPLETE"},
        {PROBE_GL_ERROR_SEEN, "GL_ERROR_SEEN"},
    };

    for (const auto& m : markers) {
        if ((flags & m.bit) && !(emitted & m.bit)) {
            emit_tef_probe_marker(m.name);
            emitted |= m.bit;
        }
    }
    g_native_probe_emitted.store(emitted, std::memory_order_release);
}

void sample_input_state() {
    int w = 0;
    int h = 0;
    int x = 0;
    int y = 0;
    bool left = false;

    if (g_field_screen_width) patchlib_field_get_value(g_field_screen_width, nullptr, &w);
    if (g_field_screen_height) patchlib_field_get_value(g_field_screen_height, nullptr, &h);
    if (g_field_mouse_x) patchlib_field_get_value(g_field_mouse_x, nullptr, &x);
    if (g_field_mouse_y) patchlib_field_get_value(g_field_mouse_y, nullptr, &y);
    if (g_field_mouse_left) patchlib_field_get_value(g_field_mouse_left, nullptr, &left);

    if (w > 0) g_game_width.store(w, std::memory_order_relaxed);
    if (h > 0) g_game_height.store(h, std::memory_order_relaxed);
    g_mouse_x.store(x, std::memory_order_relaxed);
    g_mouse_y.store(y, std::memory_order_relaxed);
    g_mouse_left.store(left, std::memory_order_relaxed);
}

void main_update_postfix(
    patch_handle_t instance,
    void** args,
    void* result,
    const patch_method_signature_t* sig_info
) {
    (void)instance;
    (void)args;
    (void)result;
    (void)sig_info;
    sample_input_state();
    flush_native_probe_markers_on_game_thread();

    if (!g_diag_dimensions_emitted.load(std::memory_order_acquire)) {
        const int sw = g_diag_surface_w.load(std::memory_order_relaxed);
        const int sh = g_diag_surface_h.load(std::memory_order_relaxed);
        const int vw = g_diag_viewport_w.load(std::memory_order_relaxed);
        const int vh = g_diag_viewport_h.load(std::memory_order_relaxed);
        if (sw > 0 && sh > 0 && vw > 0 && vh > 0) {
            char marker[128];
            std::snprintf(marker, sizeof(marker), "EGL_SURFACE_%dx%d", sw, sh);
            emit_tef_probe_marker(marker);
            std::snprintf(
                marker, sizeof(marker), "GL_VIEWPORT_%d_%d_%d_%d",
                g_diag_viewport_x.load(std::memory_order_relaxed),
                g_diag_viewport_y.load(std::memory_order_relaxed),
                vw, vh
            );
            emit_tef_probe_marker(marker);
            const int gw = g_game_width.load(std::memory_order_relaxed);
            const int gh = g_game_height.load(std::memory_order_relaxed);
            if (gw > 0 && gh > 0) {
                std::snprintf(marker, sizeof(marker), "GAME_SIZE_%dx%d", gw, gh);
                emit_tef_probe_marker(marker);
            }
            const int relation = g_diag_surface_relation.load(std::memory_order_relaxed);
            std::snprintf(marker, sizeof(marker), "SURFACE_RELATION_%d", relation);
            emit_tef_probe_marker(marker);
            const int glerr = g_diag_gl_error.load(std::memory_order_relaxed);
            std::snprintf(marker, sizeof(marker), "GL_ERROR_0x%04X", glerr);
            emit_tef_probe_marker(marker);
            g_diag_dimensions_emitted.store(true, std::memory_order_release);
        }
    }
}

bool init_imgui_on_egl_thread(EGLDisplay dpy, EGLSurface surface, int surface_w, int surface_h) {
    if (g_imgui_ready) return true;

    if (dpy == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE || eglGetCurrentContext() == EGL_NO_CONTEXT) {
        ++g_context_miss_count;
        if (g_context_miss_count == 1 || g_context_miss_count == 60 || g_context_miss_count == 300) {
            append_runtime_log("imgui: eglSwapBuffers reached, but no current EGL context/surface yet");
        }
        return false;
    }

    g_native_probe_flags.fetch_or(PROBE_GL_CONTEXT_SEEN, std::memory_order_release);

    const GLubyte* version_raw = glGetString(GL_VERSION);
    const GLubyte* renderer_raw = glGetString(GL_RENDERER);
    const char* version = reinterpret_cast<const char*>(version_raw);
    const char* renderer = reinterpret_cast<const char*>(renderer_raw);

    if (!version) {
        append_runtime_log("imgui: GL_VERSION is NULL in eglSwapBuffers hook");
        return false;
    }

    append_runtime_logf("EGL surface=%dx%d", surface_w, surface_h);
    append_runtime_logf("GL_VERSION=%s", version);
    append_runtime_logf("GL_RENDERER=%s", renderer ? renderer : "(null)");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendPlatformName = "TerrariaEGLMouseBridge";

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(1.35f);
    style.FontScaleMain = 1.35f;

    // ES2 shader syntax is intentionally used because it is the conservative
    // Android path and is accepted by the ES2-compatible subset of newer GLES contexts.
    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        append_runtime_log("imgui: ImGui_ImplOpenGL3_Init(#version 100) failed");
        ImGui::DestroyContext();
        return false;
    }

    g_last_frame_time = std::chrono::steady_clock::now();
    g_imgui_ready = true;
    g_native_probe_flags.fetch_or(PROBE_IMGUI_READY, std::memory_order_release);
    append_runtime_log("imgui: initialized successfully inside eglSwapBuffers");
    return true;
}


void draw_fb0_probe_marker(int surface_w, int surface_h) {
    if (surface_w <= 0 || surface_h <= 0) return;

    // Two raw GLES markers. One is always at the framebuffer's bottom-left,
    // independent of orientation assumptions. The second stays near the MOD
    // launcher area.
    GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint old_scissor[4] = {0, 0, 0, 0};
    GLfloat old_clear[4] = {0.f, 0.f, 0.f, 0.f};
    glGetIntegerv(GL_SCISSOR_BOX, old_scissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);

    glEnable(GL_SCISSOR_TEST);

    glScissor(8, 8, 28, 28);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const int x2 = std::clamp(
        static_cast<int>(static_cast<float>(surface_w) * 0.555f) - 28,
        4,
        std::max(4, surface_w - 22)
    );
    const int y2 = std::max(4, surface_h - 52);
    glScissor(x2, y2, 18, 18);
    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        g_diag_gl_error.store(static_cast<int>(err), std::memory_order_relaxed);
        g_native_probe_flags.fetch_or(PROBE_GL_ERROR_SEEN, std::memory_order_release);
    }

    glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
    glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
    if (!scissor_enabled) glDisable(GL_SCISSOR_TEST);
}

void draw_overlay(int surface_w, int surface_h) {
    if (surface_w <= 0 || surface_h <= 0) return;

    GLint original_viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, original_viewport);
    g_diag_viewport_x.store(original_viewport[0], std::memory_order_relaxed);
    g_diag_viewport_y.store(original_viewport[1], std::memory_order_relaxed);
    g_diag_viewport_w.store(original_viewport[2], std::memory_order_relaxed);
    g_diag_viewport_h.store(original_viewport[3], std::memory_order_relaxed);
    if (original_viewport[2] > 0 && original_viewport[3] > 0) {
        g_native_probe_flags.fetch_or(PROBE_VIEWPORT_SEEN, std::memory_order_release);
    }

    ImGuiIO& io = ImGui::GetIO();

    const auto now = std::chrono::steady_clock::now();
    float delta = std::chrono::duration<float>(now - g_last_frame_time).count();
    g_last_frame_time = now;
    if (delta <= 0.0f || delta > 0.25f) delta = 1.0f / 60.0f;

    const int game_w = g_game_width.load(std::memory_order_relaxed);
    const int game_h = g_game_height.load(std::memory_order_relaxed);
    const int raw_mouse_x = g_mouse_x.load(std::memory_order_relaxed);
    const int raw_mouse_y = g_mouse_y.load(std::memory_order_relaxed);
    const bool mouse_left = g_mouse_left.load(std::memory_order_relaxed);

    float mouse_x = static_cast<float>(raw_mouse_x);
    float mouse_y = static_cast<float>(raw_mouse_y);
    if (game_w > 0 && game_h > 0) {
        mouse_x *= static_cast<float>(surface_w) / static_cast<float>(game_w);
        mouse_y *= static_cast<float>(surface_h) / static_cast<float>(game_h);
    }

    io.DisplaySize = ImVec2(static_cast<float>(surface_w), static_cast<float>(surface_h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = delta;
    io.AddMousePosEvent(mouse_x, mouse_y);
    if (mouse_left != g_prev_mouse_left) {
        io.AddMouseButtonEvent(0, mouse_left);
        g_prev_mouse_left = mouse_left;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Compact button placement chosen from the user's 1832x832 Android layout:
    // top-center gap between the left hotbar/inventory and right status/equipment UI.
    const float button_w = 124.0f;
    const float button_h = 58.0f;
    const float button_x = std::clamp(
        static_cast<float>(surface_w) * 0.555f,
        8.0f,
        std::max(8.0f, static_cast<float>(surface_w) - button_w - 8.0f)
    );
    const float button_y = 18.0f;

    ImGui::SetNextWindowPos(ImVec2(button_x, button_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(button_w, button_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.84f);

    const ImGuiWindowFlags button_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##ModControlCenterLauncher", nullptr, button_flags)) {
        if (ImGui::Button("MOD", ImVec2(-1.0f, -1.0f))) {
            g_panel_open = !g_panel_open;
            append_runtime_log(g_panel_open ? "ui: panel opened" : "ui: panel closed");
        }
    }
    ImGui::End();

    if (g_panel_open) {
        const float panel_w = std::min(520.0f, static_cast<float>(surface_w) * 0.72f);
        const float panel_h = 250.0f;
        const float panel_x = (static_cast<float>(surface_w) - panel_w) * 0.5f;
        const float panel_y = std::max(88.0f, (static_cast<float>(surface_h) - panel_h) * 0.32f);

        ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);

        const ImGuiWindowFlags panel_flags =
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("MOD Control Center - EGL Test", &g_panel_open, panel_flags)) {
            ImGui::TextUnformatted("Phase 1: native eglSwapBuffers / touch / persistence test");
            ImGui::Separator();
            ImGui::Spacing();

            const char* master_text = g_master_enabled ? "MASTER: ON" : "MASTER: OFF";
            if (ImGui::Button(master_text, ImVec2(-1.0f, 72.0f))) {
                g_master_enabled = !g_master_enabled;
                save_config();
                append_runtime_log(g_master_enabled ? "master: ON" : "master: OFF");
            }

            ImGui::Spacing();
            ImGui::Text("Surface: %d x %d", surface_w, surface_h);
            ImGui::Text("Touch: %.0f, %.0f  down=%s", mouse_x, mouse_y, mouse_left ? "yes" : "no");
            ImGui::Text("eglSwapBuffers calls: %llu", g_swap_count.load(std::memory_order_relaxed));
            ImGui::TextUnformatted("No gameplay values are modified in this build.");
        }
        ImGui::End();
    }

    ImGui::Render();

    // IMPORTANT: ImGui's OpenGL backend renders into whichever framebuffer is
    // currently bound. Terraria can leave an off-screen render target bound
    // immediately before eglSwapBuffers(), which makes RenderDrawData() run
    // successfully but keeps every pixel invisible on the EGL window.
    GLint previous_fbo = 0;
    GLboolean previous_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glGetBooleanv(GL_COLOR_WRITEMASK, previous_color_mask);

    if (previous_fbo != 0) {
        g_native_probe_flags.fetch_or(PROBE_FBO_NONZERO_SEEN, std::memory_order_release);
        append_runtime_logf("render: framebuffer before overlay=%d; binding default framebuffer 0", previous_fbo);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, surface_w, surface_h);
    g_native_probe_flags.fetch_or(PROBE_FBO_ZERO_BOUND, std::memory_order_release);

    const GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_status == GL_FRAMEBUFFER_COMPLETE)
        g_native_probe_flags.fetch_or(PROBE_FB_COMPLETE, std::memory_order_release);
    else
        g_native_probe_flags.fetch_or(PROBE_FB_INCOMPLETE, std::memory_order_release);

    draw_fb0_probe_marker(surface_w, surface_h);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Restore the framebuffer/color write mask/viewport Terraria left us so the hook is
    // transparent to any work performed after this function returns.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
    glColorMask(previous_color_mask[0], previous_color_mask[1], previous_color_mask[2], previous_color_mask[3]);
    glViewport(original_viewport[0], original_viewport[1], original_viewport[2], original_viewport[3]);

    g_render_count.fetch_add(1, std::memory_order_relaxed);
    g_native_probe_flags.fetch_or(PROBE_FRAME_RENDERED, std::memory_order_release);
}

void render_before_native_swap(EGLDisplay dpy, EGLSurface surface, const char* source_name) {
    const unsigned long long count = g_swap_count.fetch_add(1, std::memory_order_relaxed) + 1;
    g_native_probe_flags.fetch_or(PROBE_EGL_SWAP_SEEN, std::memory_order_release);
    if (count == 1) append_runtime_logf("egl: first native swap intercepted via %s", source_name ? source_name : "(unknown)");

    EGLint surface_w = 0;
    EGLint surface_h = 0;
    if (dpy != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE) {
        eglQuerySurface(dpy, surface, EGL_WIDTH, &surface_w);
        eglQuerySurface(dpy, surface, EGL_HEIGHT, &surface_h);
    }
    if (surface_w > 0 && surface_h > 0) {
        g_diag_surface_w.store(surface_w, std::memory_order_relaxed);
        g_diag_surface_h.store(surface_h, std::memory_order_relaxed);
    }

    const EGLDisplay old_display = eglGetCurrentDisplay();
    const EGLContext old_context = eglGetCurrentContext();
    const EGLSurface old_draw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface old_read = eglGetCurrentSurface(EGL_READ);

    bool switched = false;
    if (old_draw != EGL_NO_SURFACE) {
        g_native_probe_flags.fetch_or(PROBE_CURRENT_DRAW_SURFACE_SEEN, std::memory_order_release);
    }

    if (old_draw == surface && old_draw != EGL_NO_SURFACE) {
        g_diag_surface_relation.store(1, std::memory_order_relaxed);
        g_native_probe_flags.fetch_or(PROBE_SWAP_SURFACE_MATCH, std::memory_order_release);
    } else {
        g_diag_surface_relation.store(old_draw == EGL_NO_SURFACE ? 3 : 2, std::memory_order_relaxed);
        g_native_probe_flags.fetch_or(PROBE_SWAP_SURFACE_MISMATCH, std::memory_order_release);

        if (
            old_context != EGL_NO_CONTEXT &&
            old_display == dpy &&
            old_draw != EGL_NO_SURFACE &&
            old_read != EGL_NO_SURFACE &&
            surface != EGL_NO_SURFACE
        ) {
            if (eglMakeCurrent(dpy, surface, surface, old_context) == EGL_TRUE) {
                switched = true;
                g_native_probe_flags.fetch_or(PROBE_MAKECURRENT_OK, std::memory_order_release);
                append_runtime_log("egl: temporarily switched current draw/read surface to swap surface");
            } else {
                g_native_probe_flags.fetch_or(PROBE_MAKECURRENT_FAIL, std::memory_order_release);
                append_runtime_logf("egl: eglMakeCurrent(swap surface) failed err=0x%04X", eglGetError());
            }
        }
    }

    if (surface_w > 0 && surface_h > 0 && init_imgui_on_egl_thread(dpy, surface, surface_w, surface_h)) {
        draw_overlay(surface_w, surface_h);
    }

    if (switched) {
        if (eglMakeCurrent(old_display, old_draw, old_read, old_context) != EGL_TRUE) {
            append_runtime_logf("egl: WARNING restore eglMakeCurrent failed err=0x%04X", eglGetError());
        }
    }
}

EGLBoolean hooked_egl_swap_buffers(EGLDisplay dpy, EGLSurface surface) {
    if (g_inside_swap_proxy) {
        return g_original_egl_swap_buffers ? g_original_egl_swap_buffers(dpy, surface) : EGL_FALSE;
    }

    g_inside_swap_proxy = true;
    render_before_native_swap(dpy, surface, "eglSwapBuffers");
    EGLBoolean result = g_original_egl_swap_buffers ? g_original_egl_swap_buffers(dpy, surface) : EGL_FALSE;
    g_inside_swap_proxy = false;
    return result;
}

EGLBoolean hooked_egl_swap_buffers_with_damage_khr(EGLDisplay dpy, EGLSurface surface, EGLint* rects, EGLint n_rects) {
    if (g_inside_swap_proxy) {
        return g_original_egl_swap_buffers_with_damage_khr
            ? g_original_egl_swap_buffers_with_damage_khr(dpy, surface, rects, n_rects)
            : EGL_FALSE;
    }

    g_inside_swap_proxy = true;
    render_before_native_swap(dpy, surface, "eglSwapBuffersWithDamageKHR");
    EGLBoolean result = g_original_egl_swap_buffers_with_damage_khr
        ? g_original_egl_swap_buffers_with_damage_khr(dpy, surface, rects, n_rects)
        : EGL_FALSE;
    g_inside_swap_proxy = false;
    return result;
}

EGLBoolean hooked_egl_swap_buffers_with_damage_ext(EGLDisplay dpy, EGLSurface surface, EGLint* rects, EGLint n_rects) {
    if (g_inside_swap_proxy) {
        return g_original_egl_swap_buffers_with_damage_ext
            ? g_original_egl_swap_buffers_with_damage_ext(dpy, surface, rects, n_rects)
            : EGL_FALSE;
    }

    g_inside_swap_proxy = true;
    render_before_native_swap(dpy, surface, "eglSwapBuffersWithDamageEXT");
    EGLBoolean result = g_original_egl_swap_buffers_with_damage_ext
        ? g_original_egl_swap_buffers_with_damage_ext(dpy, surface, rects, n_rects)
        : EGL_FALSE;
    g_inside_swap_proxy = false;
    return result;
}

void shadowhook_hooked_callback(
    int error_number,
    const char* lib_name,
    const char* sym_name,
    void* sym_addr,
    void* new_addr,
    void* orig_addr,
    void* arg
) {
    (void)new_addr;
    (void)arg;
    if (error_number == 0)
        g_native_probe_flags.fetch_or(PROBE_EGL_HOOK_CALLBACK_OK, std::memory_order_release);
    else
        g_native_probe_flags.fetch_or(PROBE_EGL_HOOK_CALLBACK_FAIL, std::memory_order_release);

    append_runtime_logf(
        "shadowhook callback: err=%d lib=%s sym=%s sym_addr=%p orig=%p",
        error_number,
        lib_name ? lib_name : "(null)",
        sym_name ? sym_name : "(null)",
        sym_addr,
        orig_addr
    );
}

void free_handle(patch_handle_t& handle) {
    if (handle) {
        patchlib_free(handle);
        handle = PATCH_NULL;
    }
}

} // namespace

extern "C" void mod_control_center_init(kernel_mod_handle_t* handle) {
    g_private_dir = (handle && handle->private_dir) ? handle->private_dir : "";
    g_config_path = join_path(g_private_dir, "control_center.ini");
    g_runtime_log_path = join_path(g_private_dir, "imgui_runtime.log");

    if (!g_runtime_log_path.empty()) {
        FILE* f = std::fopen(g_runtime_log_path.c_str(), "w");
        if (f) {
            std::fprintf(f, "MOD Control Center v0.1.6 - EGL draw-surface / viewport fix\n");
            std::fclose(f);
        }
    }

    load_config();

    // Managed hook only samples Terraria's touch/mouse state. Rendering happens
    // exclusively in the native EGL swap hook, so patchlib is never called from the EGL thread.
    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type) {
        g_field_screen_width = patchlib_type_get_field(main_type, "screenWidth");
        g_field_screen_height = patchlib_type_get_field(main_type, "screenHeight");
        g_field_mouse_x = patchlib_type_get_field(main_type, "mouseX");
        g_field_mouse_y = patchlib_type_get_field(main_type, "mouseY");
        g_field_mouse_left = patchlib_type_get_field(main_type, "mouseLeft");

        patch_handle_t update = patchlib_type_get_method_by_param_count(main_type, "Update", 1);
        if (!update) update = patchlib_type_get_method(main_type, "Update");
        if (update) {
            g_input_hook = patchlib_install_prepost_hook(update, nullptr, main_update_postfix);
            append_runtime_logf("input: Main.Update postfix hook id=%d", (int)g_input_hook);
            patchlib_free(update);
        } else {
            append_runtime_log("input: Main.Update not found; UI can render but touch may not work");
        }

        patchlib_free(main_type);
    } else {
        append_runtime_log("input: Terraria.Main type not found");
    }

    const int sh_init = shadowhook_init(SHADOWHOOK_MODE_SHARED, true);
    if (sh_init != 0) {
        g_native_probe_flags.fetch_or(PROBE_SHADOWHOOK_INIT_FAIL, std::memory_order_release);
        emit_tef_probe_marker("SHADOWHOOK_INIT_FAIL_IMMEDIATE");
        append_runtime_logf("shadowhook: init failed err=%d msg=%s", sh_init, shadowhook_to_errmsg(sh_init));
        return;
    }
    g_native_probe_flags.fetch_or(PROBE_SHADOWHOOK_INIT_OK, std::memory_order_release);
    emit_tef_probe_marker("SHADOWHOOK_INIT_OK_IMMEDIATE");
    append_runtime_log("shadowhook: initialized in SHARED mode (v1.0.10 static path)");

    g_swap_hook_stub = shadowhook_hook_sym_name_callback(
        "libEGL.so",
        "eglSwapBuffers",
        reinterpret_cast<void*>(hooked_egl_swap_buffers),
        reinterpret_cast<void**>(&g_original_egl_swap_buffers),
        shadowhook_hooked_callback,
        nullptr
    );

    if (!g_swap_hook_stub) {
        g_native_probe_flags.fetch_or(PROBE_EGL_HOOK_REQUEST_FAIL, std::memory_order_release);
        emit_tef_probe_marker("EGL_HOOK_REQUEST_FAIL_IMMEDIATE");
        const int err = shadowhook_get_errno();
        append_runtime_logf("shadowhook: eglSwapBuffers hook failed err=%d msg=%s", err, shadowhook_to_errmsg(err));
    } else {
        g_native_probe_flags.fetch_or(PROBE_EGL_HOOK_REQUEST_OK, std::memory_order_release);
        emit_tef_probe_marker("EGL_HOOK_REQUEST_OK_IMMEDIATE");
        append_runtime_logf("shadowhook: eglSwapBuffers hook stub=%p orig=%p", g_swap_hook_stub, reinterpret_cast<void*>(g_original_egl_swap_buffers));
    }

    // Some Android render paths use the damage variants directly. Hook them too.
    // A thread-local recursion guard prevents double rendering if one variant calls another internally.
    g_swap_damage_khr_hook_stub = shadowhook_hook_sym_name_callback(
        "libEGL.so",
        "eglSwapBuffersWithDamageKHR",
        reinterpret_cast<void*>(hooked_egl_swap_buffers_with_damage_khr),
        reinterpret_cast<void**>(&g_original_egl_swap_buffers_with_damage_khr),
        shadowhook_hooked_callback,
        nullptr
    );
    if (!g_swap_damage_khr_hook_stub) {
        const int err = shadowhook_get_errno();
        append_runtime_logf("shadowhook: eglSwapBuffersWithDamageKHR hook unavailable err=%d msg=%s", err, shadowhook_to_errmsg(err));
    } else {
        append_runtime_logf("shadowhook: eglSwapBuffersWithDamageKHR stub=%p", g_swap_damage_khr_hook_stub);
    }

    g_swap_damage_ext_hook_stub = shadowhook_hook_sym_name_callback(
        "libEGL.so",
        "eglSwapBuffersWithDamageEXT",
        reinterpret_cast<void*>(hooked_egl_swap_buffers_with_damage_ext),
        reinterpret_cast<void**>(&g_original_egl_swap_buffers_with_damage_ext),
        shadowhook_hooked_callback,
        nullptr
    );
    if (!g_swap_damage_ext_hook_stub) {
        const int err = shadowhook_get_errno();
        append_runtime_logf("shadowhook: eglSwapBuffersWithDamageEXT hook unavailable err=%d msg=%s", err, shadowhook_to_errmsg(err));
    } else {
        append_runtime_logf("shadowhook: eglSwapBuffersWithDamageEXT stub=%p", g_swap_damage_ext_hook_stub);
    }
}

extern "C" void mod_control_center_cleanup(void) {
    if (g_input_hook != PATCH_HOOK_INVALID_ID) {
        patchlib_uninstall_hook(g_input_hook);
        g_input_hook = PATCH_HOOK_INVALID_ID;
    }

    if (g_swap_damage_ext_hook_stub) {
        shadowhook_unhook(g_swap_damage_ext_hook_stub);
        g_swap_damage_ext_hook_stub = nullptr;
    }
    if (g_swap_damage_khr_hook_stub) {
        shadowhook_unhook(g_swap_damage_khr_hook_stub);
        g_swap_damage_khr_hook_stub = nullptr;
    }
    if (g_swap_hook_stub) {
        const int rc = shadowhook_unhook(g_swap_hook_stub);
        append_runtime_logf("shadowhook: eglSwapBuffers unhook rc=%d", rc);
        g_swap_hook_stub = nullptr;
    }

    if (g_imgui_ready) {
        if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        g_imgui_ready = false;
    }

    free_handle(g_field_screen_width);
    free_handle(g_field_screen_height);
    free_handle(g_field_mouse_x);
    free_handle(g_field_mouse_y);
    free_handle(g_field_mouse_left);

    append_runtime_logf(
        "cleanup: swaps=%llu renders=%llu",
        g_swap_count.load(std::memory_order_relaxed),
        g_render_count.load(std::memory_order_relaxed)
    );
}
