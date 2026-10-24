#pragma once

#include "dmfr/common/event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DMFR_EXPORT __attribute__((visibility("default")))

typedef struct dmfr_plugin_api {
    int version;
    const char* (*get_name)(void);
    const char* (*get_version)(void);
    int (*initialize)(void);
    int (*shutdown)(void);
    int (*poll_events)(dmfr::Event* events, size_t max_events, size_t* out_count);
} dmfr_plugin_api_t;

DMFR_EXPORT int dmfr_plugin_init(dmfr_plugin_api_t* api);
DMFR_EXPORT void dmfr_plugin_shutdown(void);

#ifdef __cplusplus
}
#endif
