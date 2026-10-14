/* 采样任务独占算法状态；仅投递事件，绝不等待定位、TLS 或 SMTP。 */
#include "fall_app.h"
#include "app_state.h"
#include "app_config.h"
#include "app_calib.h"
#include "app_service.h"
#include "imu_device.h"
#include "imu_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

_Static_assert(CONFIG_FALL_IMU_SDA_GPIO != CONFIG_MAIL_DTU_TX_GPIO &&
               CONFIG_FALL_IMU_SDA_GPIO != CONFIG_MAIL_DTU_RX_GPIO &&
               CONFIG_FALL_IMU_SCL_GPIO != CONFIG_MAIL_DTU_TX_GPIO &&
               CONFIG_FALL_IMU_SCL_GPIO != CONFIG_MAIL_DTU_RX_GPIO,
               "MPU6050 I2C pins must not overlap DTU UART pins");
_Static_assert(CONFIG_FALL_IMU_PERIOD_MS * CONFIG_FREERTOS_HZ >= 1000,
               "IMU sampling period must be at least one RTOS tick");

static app_state_t app;
static const char *TAG = "fall_app";
static struct { uint32_t id; uint64_t time_us; } pending[16];
static unsigned pending_head, pending_count;

static void flush_pending(void)
{
    if (!pending_count) return;
    esp_err_t err = app_service_fall_request(pending[pending_head].id,
                                            pending[pending_head].time_us);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "FALL_QUEUED event=%lu", (unsigned long)pending[pending_head].id);
        pending_head = (pending_head + 1) % 16;
        --pending_count;
    }
}

static void handle_events(uint32_t events, uint64_t now_us)
{
    if (events & FALL_EV_SUSPECTED) {
        ++app.event_seq;
        if (pending_count < 16) {
            unsigned tail = (pending_head + pending_count) % 16;
            pending[tail].id = app.event_seq;
            pending[tail].time_us = now_us;
            ++pending_count;
        } else {
            ESP_LOGE(TAG, "FALL_NOT_QUEUED event=%lu: pending queue full",
                     (unsigned long)app.event_seq);
        }
        /* 五秒抑制窗口避免同一段动作重复告警；网络处理与检测独立。 */
        fall_detector_rearm(&app.det, now_us);
    }
}

static void sample_callback(const imu_sample_t *raw, void *arg)
{
    (void)arg;
    imu_sample_t sample = *raw;
    imu_calib_apply(&app.calib, &sample);
    euler_t euler;
    att_source_t source = attitude_update(&app.att, &sample, &euler);
    handle_events(fall_detector_update(&app.det, &sample, &euler, source), sample.t_us);
}

static void sample_task(void *unused)
{
    (void)unused;
    app_configure(&app);
    esp_err_t err = imu_port_init(CONFIG_FALL_IMU_I2C_PORT, CONFIG_FALL_IMU_SDA_GPIO,
                                 CONFIG_FALL_IMU_SCL_GPIO, CONFIG_FALL_IMU_I2C_HZ,
                                 CONFIG_FALL_IMU_ADDR_8BIT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s; detection unavailable", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    bool alive = imu_device_init() == ESP_OK;
    if (alive && CONFIG_FALL_CALIB_MS > 0) app_calib_run(&app.calib);
    fall_detector_set_imu_ok(&app.det, alive, imu_device_now_us());
    ESP_LOGI(TAG, "MPU6050 SDA=%d SCL=%d ready=%d; immediate fall notification",
             CONFIG_FALL_IMU_SDA_GPIO, CONFIG_FALL_IMU_SCL_GPIO, alive);
    TickType_t wake = xTaskGetTickCount();
    uint64_t next_health_us = 0;
    for (;;) {
        uint64_t now = imu_device_now_us();
        if (now >= next_health_us) {
            /* 丢失设备后重新配置寄存器，不能只凭 WHO_AM_I 恢复采样。 */
            alive = imu_device_present() ? imu_device_check_alive() == ESP_OK
                                         : imu_device_init() == ESP_OK;
            fall_detector_set_imu_ok(&app.det, alive, now);
            if (!alive) ESP_LOGW(TAG, "IMU unavailable; detection suspended");
            next_health_us = now + 1000000;
        }
        if (imu_device_present()) {
            (void)imu_device_read(sample_callback, NULL);
            if (!imu_device_present()) fall_detector_set_imu_ok(&app.det, false, now);
        }
        handle_events(fall_detector_tick(&app.det, imu_device_now_us()), imu_device_now_us());
        /* 锁争用或网络队列已满时保留事件，下次采样循环重试入队。 */
        flush_pending();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONFIG_FALL_IMU_PERIOD_MS));
    }
}

esp_err_t fall_app_start(void)
{
    return xTaskCreatePinnedToCore(sample_task, "fall_sampling", 6144, NULL, 8, NULL, 1)
           == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
