/*
 * 应用启动入口：先建立业务服务与工作任务，再启动 UART0 调试控制台。
 */
#include "app_service.h"
#include "app_console.h"
#include "esp_err.h"
#include "fall_app.h"
#include "activity_service.h"
#include "sdkconfig.h"
#include "esp_log.h"
#if CONFIG_CLOUD_AI_ENABLE
#include "cloud_ai.h"
#endif

/*
 * 启动顺序必须保证 Service 已创建互斥锁和队列，控制台命令才能访问服务。
 */
void app_main(void)
{
    ESP_ERROR_CHECK(app_service_start());
    ESP_ERROR_CHECK(fall_app_start());
#if CONFIG_CLOUD_AI_ENABLE
    activity_register_cloud(cloud_ai_request_conversation,cloud_ai_conversation_stopped);
#endif
    /* 前台互斥调度不控制fall_app；视觉UART未配置时仍保留KEY0。 */
    esp_err_t activity_err=activity_service_start();
    if(activity_err!=ESP_OK)ESP_LOGE("main","Activity service failed: %s",esp_err_to_name(activity_err));
    /* 命令列表先登记，再启动REPL读取，避免注册与命令查询并发。 */
    ESP_ERROR_CHECK(app_console_start());
#if CONFIG_CLOUD_AI_ENABLE
    /* 联网/音频失败不停止已经启动的摔倒检测任务。 */
    esp_err_t cloud_err = cloud_ai_start();
    if (cloud_err != ESP_OK) ESP_LOGE("main", "Cloud task failed: %s", esp_err_to_name(cloud_err));
#endif
}
