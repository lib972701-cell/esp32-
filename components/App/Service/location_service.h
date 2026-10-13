/*
 * 异步独立定位接口：请求成功后通过 get_last 读取结果；仅 VALID 的坐标可使用。
 * query_age_ms 统计查询完成后的时间，不代表 GNSS 解算数据的年龄。
 */
#pragma once
#include "esp_err.h"
#include "location_protocol.h"
/* Returns queued/ESP_ERR_INVALID_STATE; result is delivered later through status. */
esp_err_t location_request(location_source_t source);
/* No coordinates are valid unless status==VALID. Age measures query completion, not fix age. */
esp_err_t location_get_last(location_result_t *out, uint32_t *query_age_ms);
