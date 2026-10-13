/*
 * 启动配置示例：复制到本地 private/mail_config.h 后填写；不在公共代码中放真实凭据。
 * ENABLE 为每次启动发送一次测试邮件的开关；授权码会编译进固件。
 * TIME_OFFSET 描述模块报告时区，更换模块或固件后应核验，不可用来伪造当前时间。
 */
#pragma once
/* Copy to private/mail_config.h locally. Never commit real credentials. */
#define MAIL_BOOT_ENABLE 0
#define MAIL_BOOT_SENDER ""
#define MAIL_BOOT_RECIPIENT ""
#define MAIL_BOOT_AUTHORIZATION ""
/* 0=DTU returns UTC; 480=DTU returns UTC+8. Verify on actual firmware. */
#define MAIL_BOOT_TIME_OFFSET_MINUTES 0
