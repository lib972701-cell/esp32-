/*
 * DTU 定位查询适配：管理命令、截止时间及失败清理，不管理业务队列。
 */
/* DTU定位查询：封装命令及超时，UART硬件由BSP提供。 */
#include "location_query.h"
#include "dtu_uart.h"
#include "esp_timer.h"
#include <string.h>
/*
 * 只能在共享 DTU worker 中独占调用；LBS 预算 65 秒，GNSS 预算 5 秒。
 * GNSS 命令读取模块现有报告，不承诺在等待期间完成首次搜星。
 * 解析首次匹配来源的应答并记录耗时；非 VALID 结果统一清除经纬度。
 */
location_result_t location_query(location_source_t source)
{
    location_result_t result={.source=source,.status=LOCATION_IO_ERROR};
    int64_t start=esp_timer_get_time();
    if (!dtu_uart_ready()) return result;
    if (source!=LOCATION_LBS && source!=LOCATION_GNSS) {
        result.status=LOCATION_BAD_REPLY;return result;
    }
    dtu_uart_flush();
    const char *command=source==LOCATION_LBS ? "config,get,lbsloc\r\n" : "config,get,gpsext\r\n";
    const char *prefix=source==LOCATION_LBS ? "config,lbsloc," : "config,gps,";
    int64_t end=start+(source==LOCATION_LBS ? 65000000LL : 5000000LL);
    if (dtu_uart_write((const unsigned char *)command,strlen(command))) {
        char line[384];result.status=LOCATION_TIMEOUT;
        while (dtu_uart_read_line(line,sizeof(line)-2,end)) {
            if (!strncmp(line,prefix,strlen(prefix))) {
                strcat(line,"\r\n");
                location_parse(line,source,&result);
                break;
            }
        }
        if (!dtu_uart_ok() || (result.status==LOCATION_TIMEOUT && esp_timer_get_time()<end))
            result.status=LOCATION_IO_ERROR;
    }
    result.elapsed_ms=(uint32_t)((esp_timer_get_time()-start)/1000);
    if (result.status!=LOCATION_VALID) result.longitude=result.latitude=0;
    return result;
}

