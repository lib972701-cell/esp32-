/*
 * DTU 板级传输层：UART1 的安装、字节收发、ASCII CRLF 读行和硬件事件检查。
 * 不拥有业务锁；调用方应串行安排查询和 TLS，避免共享流中的应答相互干扰。
 */
/*
 * DTU 板级传输层：UART1 的安装、字节收发、ASCII CRLF 读行和硬件事件检查。
 * 不拥有业务锁；调用方应串行安排查询和 TLS，避免共享流中的应答相互干扰。
 */
/* 板级DTU UART驱动：只处理硬件，不包含定位或邮件业务。 */
#include "dtu_uart.h"
#include "dtu_frame.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#define DTU_UART UART_NUM_1
_Static_assert(CONFIG_MAIL_DTU_TX_GPIO != CONFIG_MAIL_DTU_RX_GPIO,"TX and RX must differ");
static const char *TAG="dtu_uart";
static QueueHandle_t uart_events;
static bool installed;
/*
 * 幂等初始化 UART1：115200/8N1、无流控，TX/RX 由 Kconfig 选择。
 * 安装 RX 32 KiB、TX 2 KiB 缓冲区及 64 项事件队列；失败返回 ESP-IDF 错误。
 */
/*
 * 幂等初始化 UART1：115200/8N1、无流控，TX/RX 由 Kconfig 选择。
 * 安装 RX 32 KiB、TX 2 KiB 缓冲区及 64 项事件队列；失败返回 ESP-IDF 错误。
 */
esp_err_t dtu_uart_init(void)
{
    if (installed) return ESP_OK;
    uart_config_t cfg={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t e=uart_param_config(DTU_UART,&cfg);
    if (e!=ESP_OK) return e;
    e=uart_set_pin(DTU_UART,CONFIG_MAIL_DTU_TX_GPIO,CONFIG_MAIL_DTU_RX_GPIO,
                   UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if (e!=ESP_OK) return e;
    e=uart_driver_install(DTU_UART,32768,2048,64,&uart_events,0);
    if (e!=ESP_OK) return e;
    installed=true;
    return ESP_OK;
}

/*
 * 消费待处理 UART 事件，遇到溢出、缓冲区满、校验或帧错误立即报告失败。
 */
/*
 * 消费待处理 UART 事件，遇到溢出、缓冲区满、校验或帧错误立即报告失败。
 */
bool dtu_uart_ok(void)
{
    if (!installed) return false;
    uart_event_t e;
    while (xQueueReceive(uart_events,&e,0)==pdTRUE) {
        if (e.type==UART_FIFO_OVF || e.type==UART_BUFFER_FULL ||
            e.type==UART_PARITY_ERR || e.type==UART_FRAME_ERR) {
            ESP_LOGE(TAG,"UART integrity failure type=%d",e.type);
            return false;
        }
    }
    return true;
}

/*
 * 最多 512 字节分块发送，每块等待 TX 完成并检查 UART；成功表示本地发送完成。
 */
/*
 * 最多 512 字节分块发送，每块等待 TX 完成并检查 UART；成功表示本地发送完成。
 */
bool dtu_uart_write(const unsigned char *buf, size_t n)
{
    if (!installed || (!buf && n)) return false;
    while (n) {
        size_t chunk=n>512 ? 512 : n;
        if (!dtu_uart_ok() || uart_write_bytes(DTU_UART,buf,chunk)!=(int)chunk ||
            uart_wait_tx_done(DTU_UART,pdMS_TO_TICKS(2000))!=ESP_OK) return false;
        buf+=chunk; n-=chunk;
    }
    return true;
}

/*
 * 读取完整 ASCII CRLF 帧并去除行尾，deadline 是单调时钟的微秒绝对截止时间。
 * 拒绝 NUL、非法字符、超长帧和单独 LF；只用于命令应答，不能用于 TLS 二进制。
 */
/*
 * 读取完整 ASCII CRLF 帧并去除行尾，deadline 是单调时钟的微秒绝对截止时间。
 * 拒绝 NUL、非法字符、超长帧和单独 LF；只用于命令应答，不能用于 TLS 二进制。
 */
bool dtu_uart_read_line(char *line, size_t size, int64_t end)
{
    if (!installed || !line || size<3) return false;
    size_t n=0;
    while (esp_timer_get_time()<end) {
        unsigned char b;
        if (!dtu_uart_ok()) return false;
        int r=uart_read_bytes(DTU_UART,&b,1,pdMS_TO_TICKS(100));
        if (r<0) return false;
        if (r==0) continue;
        /* Never let embedded NUL truncate a frame before validation. */
        if (!dtu_frame_byte_valid(b)) return false;
        if (n+1>=size) return false;
        line[n++]=(char)b;
        if (b=='\n') {
            if (n<2 || line[n-2]!='\r') return false;
            line[n-2]=0;
            return true;
        }
    }
    return false;
}


/*
 * 仅表示驱动安装成功，不证明 DTU 已启动、接线正确或 TCP 已连接。
 */
/*
 * 仅表示驱动安装成功，不证明 DTU 已启动、接线正确或 TCP 已连接。
 */
bool dtu_uart_ready(void) { return installed; }
/*
 * 丢弃接收缓存及待处理事件；只应在新命令或新会话边界调用。
 */
/*
 * 丢弃接收缓存及待处理事件；只应在新命令或新会话边界调用。
 */
void dtu_uart_flush(void) {
    if (!installed) return;
    uart_flush_input(DTU_UART);xQueueReset(uart_events);
}
/*
 * 按毫秒超时读取二进制字节；返回实际字节数，0 表示暂无数据，负值表示失败。
 */
/*
 * 按毫秒超时读取二进制字节；返回实际字节数，0 表示暂无数据，负值表示失败。
 */
int dtu_uart_read(unsigned char *buf, size_t n, unsigned timeout_ms) {
    if (!installed || !dtu_uart_ok()) return -1;
    return uart_read_bytes(DTU_UART,buf,n,pdMS_TO_TICKS(timeout_ms));
}
