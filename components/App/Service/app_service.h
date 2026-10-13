/*
 * 应用服务接口。先调用 start；其余操作用于普通任务上下文，不可在 ISR 调用。
 * status 输出需非空。配置 begin/finish 必须配对；忙时拒绝新的修改或业务请求。
 * clock 表示已成功设置系统时间；result 仅在 result_ready 为 true 时表示完成结果。
 */
#pragma once
#include <stdint.h>
#include "mail_link.h"
typedef struct {
    bool wiring, clock, configured, busy, result_ready;
    mail_result_t result;
} app_service_status_t;
esp_err_t app_service_start(void);
/* 非阻塞复制事件到有界队列；ESP_OK 只表示入队，不表示已送达。 */
esp_err_t app_service_fall_request(uint32_t event_id, uint64_t event_us);
void app_service_status(app_service_status_t *out);
esp_err_t app_service_enable_uart(void);
esp_err_t app_service_set_time(int64_t epoch);
esp_err_t app_service_config_begin(void);
/* NULL cancels setup and clears credentials. Caller must have reserved setup. */
esp_err_t app_service_config_finish(const mail_credentials_t *credentials);
esp_err_t app_service_clear(void);
esp_err_t app_service_probe(void);
esp_err_t app_service_clock_request(void);
esp_err_t app_service_location_request(location_source_t source, bool show);
