#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
bool vision_uart_configured(void);
esp_err_t vision_uart_init(void);
int vision_uart_read(uint8_t *data,size_t capacity);
bool vision_uart_write(const char *data,size_t size);
bool vision_uart_ok(void);
