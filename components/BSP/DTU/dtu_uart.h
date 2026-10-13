/*
 * UART1 板级接口：普通任务上下文调用，业务查询和传输由共享 worker 串行使用。
 * write/read 处理字节，read_line 处理 ASCII CRLF；截止时间是 esp_timer 单调微秒。
 * ready 只反映驱动安装状态，flush 丢弃接收数据，均不代表网络状态。
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
/* UART1, board-specific pins in Kconfig. Single application worker owns all calls. */
esp_err_t dtu_uart_init(void);
bool dtu_uart_ready(void);
bool dtu_uart_ok(void);
void dtu_uart_flush(void);
bool dtu_uart_write(const unsigned char *buf, size_t n);
int dtu_uart_read(unsigned char *buf, size_t n, unsigned timeout_ms);
/* Complete ASCII CRLF line, returned without CRLF. Deadline is monotonic microseconds. */
bool dtu_uart_read_line(char *line, size_t capacity, int64_t deadline);
