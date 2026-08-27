#ifndef MOD_CONTROL_CENTER_H
#define MOD_CONTROL_CENTER_H

#include "mod_core.h"

#ifdef __cplusplus
extern "C" {
#endif

void mod_control_center_init(kernel_mod_handle_t* handle);
void mod_control_center_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
