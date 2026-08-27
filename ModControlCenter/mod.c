#include <stddef.h>

#include "mod_core.h"
#include "mod_logger.h"
#include "control_center.h"

void (*mod_logger_write)(
    mod_log_level_t level,
    const char* tag,
    const char* fmt,
    ...
) = NULL;

static void init_mod(kernel_mod_handle_t* handle) {
    mod_logger_write(
        MOD_LOG_LEVEL_INFO,
        "ModControlCenter",
        "MOD Control Center v0.1.0 initializing (author: liuxin)"
    );

    mod_control_center_init(handle);

    mod_logger_write(
        MOD_LOG_LEVEL_INFO,
        "ModControlCenter",
        "MOD Control Center v0.1.0 initialized"
    );
}

static void cleanup_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    mod_control_center_cleanup();

    mod_logger_write(
        MOD_LOG_LEVEL_INFO,
        "ModControlCenter",
        "MOD Control Center v0.1.0 unloaded"
    );
}

static kernel_mod_info_t g_info = {
    .pkg_id = "celso.modcontrolcenter",
    .version_code = 202608280,
    .api_version = 1,
    .version = "0.1.0"
};

static kernel_mod_info_t* get_info(void) {
    return &g_info;
}

static kernel_mod_ops_t g_ops = {
    init_mod,
    cleanup_mod,
    get_info
};

kernel_mod_ops_t* create_kernel_mod(void) {
    return &g_ops;
}
