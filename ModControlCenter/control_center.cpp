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

patch_hook_id_t g_draw_hook = PATCH_HOOK_INVALID_ID;

patch_handle_t g_field_screen_width = PATCH_NULL;
patch_handle_t g_field_screen_height = PATCH_NULL;
patch_handle_t g_field_mouse_x = PATCH_NULL;
patch_handle_t g_field_mouse_y = PATCH_NULL;
patch_handle_t g_field_mouse_left = PATCH_NULL;

bool g_imgui_ready = false;
int g_init_attempts = 0;
bool g_init_permanently_disabled = false;
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

bool init_imgui_if_possible() {
    if (g_imgui_ready) {
        return true;
    }

    if (g_init_permanently_disabled) {
        return false;
    }

    ++g_init_attempts;

    const GLubyte* version_raw = glGetString(GL_VERSION);
    const GLubyte* renderer_raw = glGetString(GL_RENDERER);

    const char* version = reinterpret_cast<const char*>(version_raw);
    const char* renderer = reinterpret_cast<const char*>(renderer_raw);

    append_runtime_logf("GL_VERSION=", version);
    append_runtime_logf("GL_RENDERER=", renderer);

    if (!version || std::strstr(version, "OpenGL ES") == nullptr) {
        // During early startup the managed Draw hook may run before a GL context is
        // current. Retry for a limited number of frames instead of disabling forever.
        if (g_init_attempts == 1 || g_init_attempts == 60 || g_init_attempts == 180) {
            append_runtime_log("imgui: no current OpenGL ES context yet; retrying");
        }
        if (g_init_attempts >= 180) {
            g_init_permanently_disabled = true;
            append_runtime_log("imgui: no GL context after 180 attempts; overlay disabled safely");
        }
        return false;
    }

    // Terraria Android 1.4.5.6.4 uses OpenGL ES 2.0.
    // Dear ImGui's OpenGL backend supports GLES2 when compiled with
    // IMGUI_IMPL_OPENGL_ES2 and initialized with GLSL ES 1.00.
    if (std::strstr(version, "OpenGL ES 2") == nullptr &&
        std::strstr(version, "OpenGL ES 3") == nullptr) {
        append_runtime_log("imgui: unsupported OpenGL ES version; overlay disabled safely");
        g_init_permanently_disabled = true;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendPlatformName = "TerrariaMainMouseBridge";

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(1.75f);
    style.FontScaleMain = 1.75f;

    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        append_runtime_log("imgui: ImGui_ImplOpenGL3_Init failed");
        ImGui::DestroyContext();
        g_init_permanently_disabled = true;
        return false;
    }

    g_last_frame_time = std::chrono::steady_clock::now();
    g_imgui_ready = true;
    append_runtime_log("imgui: initialized successfully");

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

    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
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

    ImGui::SetNextWindowPos(ImVec2(24.0f, 90.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_width, panel_height), ImGuiCond_Always);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("MOD Control Center - UI Test", nullptr, flags)) {
        ImGui::TextUnformatted("Phase 1: UI / touch / persistence test only");
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
        ImGui::TextUnformatted("No gameplay values are modified in this build.");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void main_draw_postfix(
    patch_handle_t instance,
    void** args,
    void* result,
    const patch_method_signature_t* sig_info
) {
    (void)instance;
    (void)args;
    (void)result;
    (void)sig_info;

    int width = 0;
    int height = 0;
    int mouse_x = 0;
    int mouse_y = 0;
    bool mouse_left = false;

    if (!read_main_input(width, height, mouse_x, mouse_y, mouse_left)) {
        return;
    }

    if (!init_imgui_if_possible()) {
        return;
    }

    draw_control_center(width, height, mouse_x, mouse_y, mouse_left);
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

    // Truncate the previous diagnostic log on each process start.
    if (!g_runtime_log_path.empty()) {
        FILE* file = std::fopen(g_runtime_log_path.c_str(), "w");
        if (file) {
            std::fprintf(file, "MOD Control Center v0.1.1\n");
            std::fclose(file);
        }
    }

    load_config();

    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (!main_type) {
        append_runtime_log("init: Terraria.Main type not found");
        return;
    }

    g_field_screen_width = patchlib_type_get_field(main_type, "screenWidth");
    g_field_screen_height = patchlib_type_get_field(main_type, "screenHeight");
    g_field_mouse_x = patchlib_type_get_field(main_type, "mouseX");
    g_field_mouse_y = patchlib_type_get_field(main_type, "mouseY");
    g_field_mouse_left = patchlib_type_get_field(main_type, "mouseLeft");

    // This exact method was confirmed by the user's Android 1.4.5.6.4 runtime probe.
    patch_handle_t draw = patchlib_type_get_method_by_param_count(main_type, "Draw", 1);
    if (!draw) {
        // Fallback: direct-by-name lookup, useful if IL2CPP metadata matching changes.
        draw = patchlib_type_get_method(main_type, "Draw");
    }

    if (!g_field_screen_width ||
        !g_field_screen_height ||
        !g_field_mouse_x ||
        !g_field_mouse_y ||
        !g_field_mouse_left) {
        append_runtime_log("init: one or more Terraria.Main input/display fields are missing");
    }

    if (!draw) {
        append_runtime_log("init: Terraria.Main.Draw not found");
    } else {
        g_draw_hook = patchlib_install_prepost_hook(draw, NULL, main_draw_postfix);
        if (g_draw_hook == PATCH_HOOK_INVALID_ID) {
            append_runtime_log("init: Main.Draw hook installation failed");
        } else {
            append_runtime_log("init: Main.Draw postfix hook installed");
        }
        patchlib_free(draw);
    }

    patchlib_free(main_type);
}

extern "C" void mod_control_center_cleanup(void) {
    if (g_draw_hook != PATCH_HOOK_INVALID_ID) {
        patchlib_uninstall_hook(g_draw_hook);
        g_draw_hook = PATCH_HOOK_INVALID_ID;
    }

    // Only issue GL cleanup calls when a context is current. If not, leaving the
    // renderer objects for process teardown is safer than crashing during unload.
    if (g_imgui_ready) {
        const GLubyte* version = glGetString(GL_VERSION);
        if (version != nullptr) {
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
