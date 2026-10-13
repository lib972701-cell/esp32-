/*
 * 启动配置适配层：读取编译期 mail_config.h；不打印凭据，也不负责 UART 或邮件传输。
 */
#include "boot_config.h"
#include "mail_config.h"
#include <string.h>
_Static_assert(sizeof(MAIL_BOOT_SENDER)<=121,"sender too long");
_Static_assert(sizeof(MAIL_BOOT_RECIPIENT)<=121,"recipient too long");
_Static_assert(sizeof(MAIL_BOOT_AUTHORIZATION)<=65,"authorization too long");
/*
 * 返回本固件是否启用上电单封测试；非零配置值视为启用。
 */
bool boot_mail_enabled(void) {return MAIL_BOOT_ENABLE!=0;}
/*
 * 返回 DTU 应答的时区偏移（分钟）；北京时间为 +480，不是 ESP32 的本地时区设置。
 */
int boot_time_offset_minutes(void) {return MAIL_BOOT_TIME_OFFSET_MINUTES;}
/*
 * 将编译配置复制到调用方结构。out 必须非空；配置缺项时返回 false，输出保持清零。
 * 这里只检查是否缺项，邮箱及授权码格式由 Service 校验；使用后的临时副本应清零。
 */
bool boot_mail_credentials(mail_credentials_t *out)
{
    if(!out)return false;
    *out=(mail_credentials_t){0};
    if(!MAIL_BOOT_SENDER[0] || !MAIL_BOOT_RECIPIENT[0] || !MAIL_BOOT_AUTHORIZATION[0])return false;
    strcpy(out->sender,MAIL_BOOT_SENDER);strcpy(out->recipient,MAIL_BOOT_RECIPIENT);
    strcpy(out->authorization,MAIL_BOOT_AUTHORIZATION);return true;
}
