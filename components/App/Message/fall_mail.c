#include "fall_mail.h"
#include <stdio.h>
#include <inttypes.h>
bool fall_mail_body(char *out, size_t capacity, uint32_t event_id, uint64_t event_us)
{
    if (!out || !capacity) return false;
    int n = snprintf(out, capacity,
        "紧急联系人您好：设备检测到佩戴者疑似摔倒，可能需要帮助。\r\n"
        "请尽快联系本人确认安全；如无法取得联系，请及时安排现场查看。\r\n"
        "事件编号：%" PRIu32 "；开机后时间：%" PRIu64 "毫秒。\r\n"
        "此为设备自动告警，请核实实际情况。", event_id, event_us / 1000);
    if (n < 0 || (size_t)n >= capacity || n > 400) {out[0] = 0; return false;}
    return true;
}
bool fall_mail_retry(mail_result_t result, unsigned attempts)
{
    /* DATA 后结果未知、明确拒收、已接受均终止；仅确定失败最多三次。 */
    return result == MAIL_FAILED && attempts < 3;
}
