#include "cloud_ai.h"
#include "application.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <exception>

static void cloud_task(void*) {
    esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) {
        /* 不自动擦除：NVS可能保存配网、绑定和其他业务配置。 */
        ESP_LOGE("cloud_ai", "NVS initialization failed: %s; fall service remains running", esp_err_to_name(e));
        Application::GetInstance().RequestConversation(false);
        vTaskDelete(nullptr);
        return;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("cloud_ai", "Event loop failed: %s", esp_err_to_name(e));
        Application::GetInstance().RequestConversation(false);
        vTaskDelete(nullptr);
        return;
    }
    try {
        Application::GetInstance().Start();
        if(!Application::GetInstance().IsReady())Application::GetInstance().RequestConversation(false);
    } catch (const std::exception& error) {
        ESP_LOGE("cloud_ai","Cloud initialization failed: %s; fall_sampling continues",error.what());
        Application::GetInstance().RequestConversation(false);
    }
    vTaskDelete(nullptr);
}
extern "C" esp_err_t cloud_ai_start(void) {
    return xTaskCreatePinnedToCore(cloud_task, "cloud_ai_init", 12288, nullptr, 3, nullptr, 0)
           == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
extern "C" bool cloud_ai_request_conversation(bool start) {
    Application::GetInstance().RequestConversation(start); return true;
}
extern "C" bool cloud_ai_conversation_stopped(void) {
    return Application::GetInstance().ConversationStopped();
}
