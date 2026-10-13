#pragma once
#include <stdint.h>
#include "mail_protocol.h"
/* 纯文本构造与重试策略，不访问硬件。 */
bool fall_mail_body(char *out, size_t capacity, uint32_t event_id, uint64_t event_us);
bool fall_mail_retry(mail_result_t result, unsigned attempts);
