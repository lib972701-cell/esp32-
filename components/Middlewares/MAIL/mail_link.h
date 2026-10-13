/*
 * 同步邮件传输接口：只允许共享 worker 独占 UART 调用，可能阻塞数分钟。
 * credentials 含 QQ SMTP 授权码而非网页登录密码，禁止打印；临时副本用后清零。
 * subject/body 数组容量含 NUL；长度按 UTF-8 字节计算。probe_only 不执行真实投递。
 */
#pragma once
#include "esp_err.h"
#include "mail_protocol.h"
#include "location_protocol.h"
typedef struct {
    char sender[121];        /* 发件邮箱地址，容量包含结尾 NUL。 */
    char recipient[121];     /* 收件邮箱地址；所有邮件使用当前服务配置。 */
    char authorization[65]; /* SMTP 授权码，敏感数据，禁止写入日志。 */
} mail_credentials_t;
typedef struct {
    char subject[46]; // <=45 UTF-8 bytes keeps a single RFC2047 word <=75 bytes.
    char body[769];
} mail_message_t;
esp_err_t mail_link_init(void);
mail_result_t mail_link_run(const mail_credentials_t *credentials, const mail_message_t *message, bool probe_only);
void mail_secure_zero(void *p, size_t n);
/* Invoke only from the shared worker, never during a mail/TLS operation. */
location_result_t mail_link_location(location_source_t source);
