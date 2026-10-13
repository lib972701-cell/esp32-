/*
 * DTU 时间查询适配：通过 BSP 读取厂商应答，再委托纯 C 解析器换算 UTC。
 */
#include "dtu_time.h"
#include "dtu_uart.h"
#include "esp_timer.h"
#include <string.h>
/*
 * 查询一次网络时间：单次等待最多 5 秒，且不超过调用方的单调微秒截止时间。
 * 清除旧接收内容后发送命令，跳过无关行；首次匹配应答即交给解析器。
 * 成功写入 UTC Unix 秒数，失败返回 false；重试预算由 Service 管理。
 */
bool dtu_time_query(int offset_minutes, int64_t *utc_epoch, int64_t deadline_us)
{
    if (!utc_epoch || !dtu_uart_ready() || esp_timer_get_time()>=deadline_us) return false;
    *utc_epoch=0;dtu_uart_flush();
    const char *command="config,get,nettime\r\n";
    if(!dtu_uart_write((const unsigned char *)command,strlen(command)))return false;
    char line[128];int64_t end=esp_timer_get_time()+5000000;
    if(end>deadline_us)end=deadline_us;
    while(dtu_uart_read_line(line,sizeof(line)-2,end)) {
        if(!strncmp(line,"config,nettime,",strlen("config,nettime,"))) {
            strcat(line,"\r\n");return dtu_time_parse(line,offset_minutes,utc_epoch);
        }
    }
    return false;
}
