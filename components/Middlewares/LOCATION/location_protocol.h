/*
 * 定位数据与纯解析接口。坐标采用代码约定的 WGS84 十进制度，经度/纬度分开存放。
 * elapsed_ms 是查询耗时，error_code 是设备应答错误；二者不表示精度或数据年龄。
 * parse 要求完整 CRLF；硬件适配层负责记录查询耗时，失败结果不能作为坐标使用。
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { LOCATION_LBS, LOCATION_GNSS } location_source_t;
typedef enum {
    LOCATION_VALID, LOCATION_NO_FIX, LOCATION_BAD_REPLY,
    LOCATION_DEVICE_ERROR, LOCATION_TIMEOUT, LOCATION_IO_ERROR
} location_status_t;
typedef struct {
    location_source_t source;
    location_status_t status;
    double longitude, latitude; /* 十进制度；仅 status 为 VALID 时有意义。 */
    uint32_t elapsed_ms;        /* 本次查询耗时，非定位数据年龄。 */
    int error_code;             /* DEVICE_ERROR 时的模块错误码。 */
} location_result_t;
/* Input must be a complete CRLF frame. Documented coordinates are decimal degrees. */
location_status_t location_parse(const char *frame, location_source_t source, location_result_t *out);
const char *location_status_name(location_status_t status);
