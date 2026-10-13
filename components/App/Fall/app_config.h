#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "app_state.h"

/* Translate Kconfig values and initialise the algorithm state once at boot. */
void app_configure(app_state_t *app);

#endif
