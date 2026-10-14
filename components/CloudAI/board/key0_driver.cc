#include "key0_driver.h"
#include "board_keys_filter.h"
#include "activity_service.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void key_task(void* context) {
    auto dev = static_cast<i2c_master_dev_handle_t>(context);
    board_keys_filter_t filters;
    board_keys_filter_init(&filters);
    unsigned errors = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        uint8_t reg = 0x01, input = 0xff; /* P1_7/6/5＝KEY0/1/2 */
        bool valid = i2c_master_transmit_receive(dev, &reg, 1, &input, 1, 20) == ESP_OK;
        if (!valid && errors++ % 100 == 0) ESP_LOGW("key0", "XL9555 read failed; key events suppressed");
        if (valid) errors = 0;
        key0_event_t events[3];
        board_keys_filter_update(&filters,valid,input,esp_timer_get_time()/1000,events);
        for(unsigned key=0;key<3;key++) {
            auto event=events[key];
            if(event==KEY0_SHORT || event==KEY0_LONG) {
                esp_err_t e=activity_key(key,event==KEY0_LONG);
                if(e!=ESP_OK)ESP_LOGW("key0","KEY%u dispatch failed: %s",key,esp_err_to_name(e));
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}
esp_err_t key0_driver_start(i2c_master_dev_handle_t expander) {
    if (!expander) return ESP_ERR_INVALID_ARG;
    return xTaskCreatePinnedToCore(key_task, "key0_scan", 4096, expander, 3, nullptr, 0)
           == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
