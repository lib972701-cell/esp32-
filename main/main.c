/*
 * 应用启动入口：先建立业务服务与工作任务，再启动 UART0 调试控制台。
 */
#include "app_service.h"
#include "app_console.h"
#include "esp_err.h"

/*
 * 启动顺序必须保证 Service 已创建互斥锁和队列，控制台命令才能访问服务。
 */
void app_main(void)
{
    ESP_ERROR_CHECK(app_service_start());
    ESP_ERROR_CHECK(app_console_start());
}
