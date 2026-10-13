/*
 * 编译期配置读取接口：输出凭据副本须由调用方清理；不进行持久化写入。
 * credentials 要求非空输出；时间偏移针对 DTU 返回日期而非本地显示时区。
 */
#pragma once
#include "mail_link.h"
bool boot_mail_enabled(void);
bool boot_mail_credentials(mail_credentials_t *out);
int boot_time_offset_minutes(void);
