/*
 * 异步邮件接口：返回 ESP_OK 仅表示任务入队，最终提交结果通过 get_last_result 获取。
 * 主题/正文限制均为 UTF-8 字节数；工作期间不接受新请求，不自动重试邮件。
 */
#pragma once
#include "esp_err.h"
#include "mail_protocol.h"
#include "location_protocol.h"
// Called from normal task context; rejects while busy. Never retries a send.
esp_err_t mail_request_test(void);
// Copies UTF-8 data into the worker queue. subject <=45 bytes, body <=768 bytes.
// Uses recipients already configured by mail_setup; requires clock and wiring.
esp_err_t mail_request_text(const char *subject, const char *body);
/* base_body <=400 bytes; query and send are one job. No valid fix still sends a note. */
esp_err_t mail_request_with_location(const char *subject, const char *base_body, location_source_t source);
/* ESP_ERR_INVALID_STATE while pending or before any mail completion. No credentials returned. */
esp_err_t mail_get_last_result(mail_result_t *out);
