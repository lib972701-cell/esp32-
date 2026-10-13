/*
 * 纯 SMTP 协议接口：I/O 回调允许短读短写，ctx 由调用方持有，stage 可为空。
 * MAIL_ACCEPTED 是 DATA 最终 250；MAIL_UNKNOWN 表示正文提交结果无法确定，不应自动重发。
 * submit 需要编码后的用户名/授权码和 MIME；时限与 TLS 校验由传输层负责。
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    void *ctx;
    int (*read)(void *, unsigned char *, size_t);
    int (*write)(void *, const unsigned char *, size_t);
    void (*stage)(void *, const char *);
} mail_io_t;
typedef enum { MAIL_FAILED, MAIL_REJECTED, MAIL_UNKNOWN, MAIL_ACCEPTED, MAIL_PROBE_OK } mail_result_t;
bool mail_address_valid(const char *s);
bool mail_dtu_channel_valid(const char *line);
int mail_reply(mail_io_t *io);
bool mail_write_all(mail_io_t *io, const char *text);
mail_result_t mail_submit(mail_io_t *io, const char *sender, const char *recipient,
                          const char *user_b64, const char *pass_b64, const char *mime);
