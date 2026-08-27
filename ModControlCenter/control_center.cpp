#include "control_center.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <GLES2/gl2.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/type.h"

namespace {

patch_hook_id_t g_present_hook = PATCH_HOOK_INVALID_ID;
patch_hook_id_t g_end_draw_hook = PATCH_HOOK_INVALID_ID;

patch_handle_t g_field_screen_width = PATCH_NULL;
patch_handle_t g_field_screen_height = PATCH_NULL;
patch_handle_t g_field_mouse_x = PATCH_NULL;
patch_handle_t g_field_mouse_y = PATCH_NULL;
patch_handle_t g_field_mouse_left = PATCH_NULL;

bool g_imgui_ready = false;
int g_context_miss_count = 0;
bool g_master_enabled = true;
bool g_prev_mouse_left = false;

std::string g_private_dir;
std::string g_config_path;
std::string g_runtime_log_path;

std::chrono::steady_clock::time_point g_last_frame_time;

void append_runtime_log(const char* text) {
    if (g_runtime_log_path.empty() || text == nullptr) {
        return;
    }

    FILE* file = std::fopen(g_runtime_log_path.c_str(), "a");
    if (!file) {
        return;
    }

    std::fprintf(file, "%s\n", text);
    std::fclose(file);
}

void append_runtime_logf(const char* prefix, const char* value) {
    if (g_runtime_log_path.empty()) {
        return;
    }

    FILE* file = std::fopen(g_runtime_log_path.c_str(), "a");
    if (!file) {
        return;
    }

    std::fprintf(file, "%s%s\n", prefix ? prefix : "", value ? value : "(null)");
    std::fclose(file);
}

void append_runtime_logi(const char* prefix, int value) {
    if (g_runtime_log_path.empty()) {
        return;
    }

    FILE* file = std::fopen(g_runtime_log_path.c_str(), "a");
    if (!file) {
        return;
    }

    std::fprintf(file, "%s%d\n", prefix ? prefix : "", value);
    std::fclose(file);
}

std::string join_path(const std::string& dir, const char* name) {
    if (dir.empty()) {
        return std::string(name ? name : "");
    }

    if (dir.back() == '/' || dir.back() == '\\') {
        return dir + (name ? name : "");
    }

    return dir + "/" + (name ? name : "");
}

void save_config() {
    if (g_config_path.empty()) {
        return;
    }

    FILE* file = std::fopen(g_config_path.c_str(), "w");
    if (!file) {
        append_runtime_log("config: save failed");
        return;
    }

    std::fprintf(file, "master=%d\n", g_master_enabled ? 1 : 0);
    std::fclose(file);
}

void load_config() {
    g_master_enabled = true;

    if (g_config_path.empty()) {
        return;
    }

    FILE* file = std::fopen(g_config_path.c_str(), "r");
    if (!file) {
        save_config();
        return;
    }

    char line[128];
    while (std::fgets(line, sizeof(line), file)) {
        int value = 0;
        if (std::sscanf(line, "master=%d", &value) == 1) {
            g_master_enabled = value != 0;
        }
    }

    std::fclose(file);
}

bool read_main_input(int& width, int& height, int& mouse_x, int& mouse_y, bool& mouse_left) {
    if (!g_field_screen_width ||
        !g_field_screen_height ||
        !g_field_mouse_x ||
        !g_field_mouse_y ||
        !g_field_mouse_left) {
        return false;
    }

    width = 0;
    height = 0;
    mouse_x = 0;
    mouse_y = 0;
    mouse_left = false;

    patchlib_field_get_value(g_field_screen_width, NULL, &width);
    patchlib_field_get_value(g_field_screen_height, NULL, &height);
    patchlib_field_get_value(g_field_mouse_x, NULL, &mouse_x);
    patchlib_field_get_value(g_field_mouse_y, NULL, &mouse_y);
    patchlib_field_get_value(g_field_mouse_left, NULL, &mouse_left);

    return width > 0 && height > 0;
}

bool has_current_gl_context() {
    const GLubyte* version_raw = glGetString(GL_VERSION);
    return version_raw != nullptr;
}

bool init_imgui_on_present_thread() {
    if (g_imgui_ready) {
        return true;
    }

    const GLubyte* version_raw = glGetString(GL_VERSION);
    const GLubyte* renderer_raw = glGetString(GL_RENDERER);

    const char* version = reinterpret_cast<const char*>(version_raw);
    const char* renderer = reinterpret_cast<const char*>(renderer_raw);

    if (!version) {
        ++g_context_miss_count;
        if (g_context_miss_count == 1 || g_context_miss_count == 60 || g_context_miss_count == 300) {
            append_runtime_log("imgui: Present hook ran without a current GL context; retrying");
        }
        return false;
    }

    append_runtime_logf("GL_VERSION=", version);
    append_runtime_logf("GL_RENDERER=", renderer);

    if (std::strstr(version, "OpenGL ES") == nullptr) {
        append_runtime_log("imgui: current context is not OpenGL ES; renderer not initialized");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendPlatformName = "TerrariaPresentMouseBridge";

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(1.75f);
    style.FontScaleMain = 1.75f;

    // GLES2 is the most conservative backend choice for Android here. It also
    // works with the ES2-compatible subset of newer GLES contexts.
    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        append_runtime_log("imgui: ImGui_ImplOpenGL3_Init(#version 100) failed");
        ImGui::DestroyContext();
        return false;
    }

    g_last_frame_time = std::chrono::steady_clock::now();
    g_imgui_ready = true;
    append_runtime_log("imgui: initialized successfully on Present thread");
    return true;
}

void draw_control_center(int width, int height, int mouse_x, int mouse_y, bool mouse_left) {
    ImGuiIO& io = ImGui::GetIO();

    const auto now = std::chrono::steady_clock::now();
    float delta = std::chrono::duration<float>(now - g_last_frame_time).count();
    g_last_frame_time = now;
    if (delta <= 0.0f || delta > 0.25f) {
        delta = 1.0f / 60.0f;
    }

    GLint viewport[4] = {0, 0, width, height};
    glGetIntegerv(GL_VIEWPORT, viewport);

    const int framebuffer_width = viewport[2] > 0 ? viewport[2] : width;
    const int framebuffer_height = viewport[3] > 0 ? viewport[3] : height;

    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(
        width > 0 ? static_cast<float>(framebuffer_width) / static_cast<float>(width) : 1.0f,
        height > 0 ? static_cast<float>(framebuffer_height) / static_cast<float>(height) : 1.0f
    );
    io.DeltaTime = delta;

    io.AddMousePosEvent(static_cast<float>(mouse_x), static_cast<float>(mouse_y));
    if (mouse_left != g_prev_mouse_left) {
        io.AddMouseButtonEvent(0, mouse_left);
        g_prev_mouse_left = mouse_left;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    const float panel_width = std::min(620.0f, static_cast<float>(width) * 0.62f);
    const float panel_height = 280.0f;

    // Test UI location: upper-left, x=24 px, y=90 px.
    ImGui::SetNextWindowPos(ImVec2(24.0f, 90.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_width, panel_height), ImGuiCond_Always);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("MOD Control Center - Present Test", nullptr, flags)) {
        ImGui::TextUnformatted("Phase 1: render-at-Present / touch / persistence test");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Master control");

        const char* button_text = g_master_enabled ? "MASTER: ON" : "MASTER: OFF";
        if (ImGui::Button(button_text, ImVec2(-1.0f, 72.0f))) {
            g_master_enabled = !g_master_enabled;
            save_config();
            append_runtime_log(g_master_enabled ? "master: ON" : "master: OFF");
        }

        ImGui::Spacing();
        ImGui::Text("Saved state: %s", g_master_enabled ? "ON" : "OFF");
        ImGui::Text("Touch: x=%d y=%d down=%s", mouse_x, mouse_y, mouse_left ? "yes" : "no");
        ImGui::Text("Viewport: %d x %d", framebuffer_width, framebuffer_height);
        ImGui::TextUnformatted("No gameplay values are modified in this build.");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void render_overlay_if_possible() {
    int width = 0;
    int height = 0;
    int mouse_x = 0;
    int mouse_y = 0;
    bool mouse_left = false;

    if (!read_main_input(width, height, mouse_x, mouse_y, mouse_left)) {
        return;
    }

    if (!init_imgui_on_present_thread()) {
        return;
    }

    draw_control_center(width, height, mouse_x, mouse_y, mouse_left);
}

bool graphics_device_present_prefix(
    patch_handle_t instance,
    void** args,
    const patch_method_signature_t* sig_info,
    void* result
) {
    (void)instance;
    (void)args;
    (void)sig_info;
    (void)result;

    // Present() has not called PlatformPresent()/swap yet. Render the overlay
    // now, then return false so MonoGame continues its normal Present().
    render_overlay_if_possible();
    return false;
}

bool graphics_device_manager_end_draw_prefix(
    patch_handle_t instance,
    void** args,
    const patch_method_signature_t* sig_info,
    void* result
) {
    (void)instance;
    (void)args;
    (void)sig_info;
    (void)result;

    // Fallback only. EndDraw() immediately calls GraphicsDevice.Present().
    render_overlay_if_possible();
    return false;
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
        FILE* file = std::fopen(g_runtime_log_path.c_str(), "w");
        if (file) {
            std::fprintf(file, "MOD Control Center v0.1.2 - Present hook test\n");
            std::fclose(file);
        }
    }

    load_config();

    // Terraria input/display values are only sampled. No gameplay state is changed.
    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type) {
        g_field_screen_width = patchlib_type_get_field(main_type, "screenWidth");
        g_field_screen_height = patchlib_type_get_field(main_type, "screenHeight");
        g_field_mouse_x = patchlib_type_get_field(main_type, "mouseX");
        g_field_mouse_y = patchlib_type_get_field(main_type, "mouseY");
        g_field_mouse_left = patchlib_type_get_field(main_type, "mouseLeft");
        patchlib_free(main_type);
    } else {
        append_runtime_log("init: Terraria.Main type not found");
    }

    // Primary render point: GraphicsDevice.Present(). MonoGame calls PlatformPresent
    // from here, so a Prefix is after game drawing but before the backbuffer swap.
    patch_handle_t graphics_device_type =
        patchlib_type_get_type("Microsoft.Xna.Framework.Graphics", "GraphicsDevice");

    if (graphics_device_type) {
        patch_handle_t present =
            patchlib_type_get_method_by_param_count(graphics_device_type, "Present", 0);
        if (!present) {
            present = patchlib_type_get_method(graphics_device_type, "Present");
        }

        if (present) {
            g_present_hook = patchlib_install_prepost_hook(
                present,
                graphics_device_present_prefix,
                NULL
            );

            if (g_present_hook != PATCH_HOOK_INVALID_ID) {
                append_runtime_logi("init: GraphicsDevice.Present prefix hook id=", (int)g_present_hook);
            } else {
                append_runtime_log("init: GraphicsDevice.Present hook installation failed");
            }
            patchlib_free(present);
        } else {
            append_runtime_log("init: GraphicsDevice.Present not found");
        }

        patchlib_free(graphics_device_type);
    } else {
        append_runtime_log("init: Microsoft.Xna.Framework.Graphics.GraphicsDevice type not found");
    }

    // Safe managed fallback if Present is not exposed by this build.
    if (g_present_hook == PATCH_HOOK_INVALID_ID) {
        patch_handle_t manager_type =
            patchlib_type_get_type("Microsoft.Xna.Framework", "GraphicsDeviceManager");

        if (manager_type) {
            patch_handle_t end_draw =
                patchlib_type_get_method_by_param_count(manager_type, "EndDraw", 0);
            if (!end_draw) {
                end_draw = patchlib_type_get_method(manager_type, "EndDraw");
            }

            if (end_draw) {
                g_end_draw_hook = patchlib_install_prepost_hook(
                    end_draw,
                    graphics_device_manager_end_draw_prefix,
                    NULL
                );

                if (g_end_draw_hook != PATCH_HOOK_INVALID_ID) {
                    append_runtime_logi("init: GraphicsDeviceManager.EndDraw fallback hook id=", (int)g_end_draw_hook);
                } else {
                    append_runtime_log("init: GraphicsDeviceManager.EndDraw hook installation failed");
                }
                patchlib_free(end_draw);
            } else {
                append_runtime_log("init: GraphicsDeviceManager.EndDraw not found");
            }

            patchlib_free(manager_type);
        } else {
            append_runtime_log("init: Microsoft.Xna.Framework.GraphicsDeviceManager type not found");
        }
    }

    if (g_present_hook == PATCH_HOOK_INVALID_ID && g_end_draw_hook == PATCH_HOOK_INVALID_ID) {
        append_runtime_log("init: no safe Present/EndDraw render hook available; overlay disabled");
    }
}

extern "C" void mod_control_center_cleanup(void) {
    if (g_present_hook != PATCH_HOOK_INVALID_ID) {
        patchlib_uninstall_hook(g_present_hook);
        g_present_hook = PATCH_HOOK_INVALID_ID;
    }

    if (g_end_draw_hook != PATCH_HOOK_INVALID_ID) {
        patchlib_uninstall_hook(g_end_draw_hook);
        g_end_draw_hook = PATCH_HOOK_INVALID_ID;
    }

    // Only use GL cleanup while a context is actually current.
    if (g_imgui_ready) {
        if (has_current_gl_context()) {
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::DestroyContext();
        }
        g_imgui_ready = false;
    }

    free_handle(g_field_screen_width);
    free_handle(g_field_screen_height);
    free_handle(g_field_mouse_x);
    free_handle(g_field_mouse_y);
    free_handle(g_field_mouse_left);

    append_runtime_log("cleanup: complete");
}
