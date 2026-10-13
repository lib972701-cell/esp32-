/*
 * 纯正文构造接口：输入本次定位结果，输出 UTF-8 文本，不访问设备或服务器。
 * out 与 base_body 不得重叠；capacity 包含结尾 NUL。失败返回 false，正文置空。
 * 基础正文最多 400 字节，完整正文最多 768 字节；query_epoch 是 UTC 查询完成秒数。
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "location_protocol.h"
/* base_body <=400 UTF-8 bytes, output <=768 bytes. No map coordinate conversion. */
bool location_mail_body(char *out, size_t capacity, const char *base_body,
                        const location_result_t *location, int64_t query_epoch);
