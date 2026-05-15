#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void hotspot_start(void);
void hotspot_stop(void);
bool hotspot_is_running(void);
int  hotspot_client_count(void);

#ifdef __cplusplus
}
#endif
