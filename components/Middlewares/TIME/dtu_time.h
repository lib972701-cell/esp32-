/*
 * 时间接口：parse 是无硬件依赖的纯解析，query 必须独占 DTU UART。
 * offset_minutes 为模块应答时区相对 UTC 的偏移；成功输出 UTC Unix 秒数。
 * frame 需要完整 CRLF，deadline_us 为单调微秒绝对截止时间，而非 Unix 时间。
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* offset_minutes describes the timezone of the DTU response, not the ESP32 locale. */
bool dtu_time_parse(const char *frame, int offset_minutes, int64_t *utc_epoch);
bool dtu_time_query(int offset_minutes, int64_t *utc_epoch, int64_t deadline_us);
