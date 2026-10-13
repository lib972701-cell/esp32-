/*
 * UART0 REPL 启动接口；应在应用服务启动成功后调用。
 */
#pragma once
#include "esp_err.h"
esp_err_t app_console_start(void);
