/*
 * 同步定位查询接口：仅共享业务 worker 独占 UART 时调用，可能等待数十秒。
 * 返回来源、状态、耗时及错误码；仅 VALID 表示解析出的坐标可使用。
 */
#pragma once
#include "location_protocol.h"
/* Must run exclusively in the application's shared UART worker. */
location_result_t location_query(location_source_t source);
