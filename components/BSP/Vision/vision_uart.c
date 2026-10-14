/* 独立业务UART2；从不安装/重配置日志UART0或DTU UART1。 */
#include "vision_uart.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

static bool installed;
static QueueHandle_t events;
bool vision_uart_configured(void) {
#if CONFIG_VISION_UART_ENABLE
    return true;
#else
    return false;
#endif
}
static bool occupied(int pin) {
    const int reserved[]={CONFIG_MAIL_DTU_TX_GPIO,CONFIG_MAIL_DTU_RX_GPIO,
        CONFIG_FALL_IMU_SDA_GPIO,CONFIG_FALL_IMU_SCL_GPIO,
        0,7,19,20,35,36,37,43,44}; /* IO7保留给现有IMU INT，另含BOOT/USB/PSRAM/UART0 */
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++)if(pin==reserved[i])return true;
#if CONFIG_CLOUD_AI_ENABLE
    const int audio[]={3,46,9,10,14,41,42};
    for(unsigned i=0;i<sizeof(audio)/sizeof(audio[0]);i++)if(pin==audio[i])return true;
#endif
    return false;
}
static bool valid_pin(int pin,bool output) {
    if(pin<0 || pin>=SOC_GPIO_PIN_COUNT)return false;
    uint64_t mask=output ? SOC_GPIO_VALID_OUTPUT_GPIO_MASK : SOC_GPIO_VALID_GPIO_MASK;
    /* SDK宏对默认-1会产生编译期负移位警告；先检查范围再查有效引脚掩码。 */
    return ((mask>>((unsigned)pin&63U))&1U)!=0;
}
esp_err_t vision_uart_init(void) {
    if(installed)return ESP_OK;
    if(!vision_uart_configured())return ESP_ERR_INVALID_STATE;
    const int tx=CONFIG_VISION_TX_GPIO,rx=CONFIG_VISION_RX_GPIO;
    if(!valid_pin(tx,true) || !valid_pin(rx,false) || tx==rx || occupied(tx) || occupied(rx))
        return ESP_ERR_INVALID_ARG;
    uart_config_t cfg={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t err=uart_param_config(UART_NUM_2,&cfg);
    if(err!=ESP_OK)return err;
    err=uart_set_pin(UART_NUM_2,tx,rx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(err!=ESP_OK)return err;
    err=uart_driver_install(UART_NUM_2,4096,0,16,&events,0);
    if(err==ESP_OK)installed=true;
    return err;
}
bool vision_uart_ok(void) {
    if(!installed)return false;
    uart_event_t event; bool ok=true;
    while(xQueueReceive(events,&event,0)==pdTRUE) {
        if(event.type==UART_FIFO_OVF || event.type==UART_BUFFER_FULL ||
           event.type==UART_FRAME_ERR || event.type==UART_PARITY_ERR)ok=false;
    }
    if(!ok) { uart_flush_input(UART_NUM_2); xQueueReset(events); }
    return ok;
}
int vision_uart_read(uint8_t *data,size_t capacity) {
    return installed ? uart_read_bytes(UART_NUM_2,data,capacity,0) : 0;
}
bool vision_uart_write(const char *data,size_t size) {
    return installed && uart_write_bytes(UART_NUM_2,data,size)==(int)size &&
        uart_wait_tx_done(UART_NUM_2,pdMS_TO_TICKS(50))==ESP_OK;
}
