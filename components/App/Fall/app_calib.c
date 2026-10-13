#include "app_calib.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "imu_calib.h"
#include "imu_device.h"

static const char *TAG = "fall_app";

/* Calibration window callback: only accumulates, nothing reaches the detector. */
static void on_calib_sample(const imu_sample_t *sample, void *arg)
{
    imu_calib_t *calib = (imu_calib_t *)arg;
    imu_calib_accumulate(calib, sample);
}

void app_calib_run(imu_calib_t *calib)
{
    ESP_LOGI(TAG, "calibrating: keep the device completely still");

    for (int attempt = 1; attempt <= CONFIG_FALL_CALIB_TRIES; attempt++) {
        imu_calib_begin(calib);

        const uint64_t deadline_us = imu_device_now_us() + ((uint64_t)CONFIG_FALL_CALIB_MS * 1000ULL);
        while (imu_device_now_us() < deadline_us) {
            (void)imu_device_read(on_calib_sample, calib);
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        if (imu_calib_finish(calib)) {
            ESP_LOGI(TAG, "calibration ok on attempt %d (%u samples): gyro bias"
                          " x=%.1f y=%.1f z=%.1f dps; |a| was %.1f mg, tilt %.1f deg;"
                          " accel correction: %s",
                     attempt, (unsigned)calib->samples,
                     calib->gyro_bias_mdps[0] / 1000.0f,
                     calib->gyro_bias_mdps[1] / 1000.0f,
                     calib->gyro_bias_mdps[2] / 1000.0f,
                     calib->acc_norm_mean_mg, calib->acc_tilt_deg,
                     calib->per_axis_acc_bias ? "per-axis offsets (window was flat)"
                                                   : "magnitude scale only (window was tilted)");
            return;
        }

        ESP_LOGW(TAG, "calibration attempt %d/%d rejected: gyro spread %.2f dps (limit %.1f),"
                      " accel spread %.3f g (limit %.3f)",
                 attempt, CONFIG_FALL_CALIB_TRIES, calib->gyro_spread_dps,
                 (double)IMU_CALIB_MAX_GYRO_SPREAD_DPS, calib->acc_spread_g,
                 (double)IMU_CALIB_MAX_ACC_SPREAD_G);
    }

    ESP_LOGW(TAG, "no still window found - running uncalibrated; yaw will drift with the gyro"
                  " offset. Reset the board with the device at rest to calibrate.");
}
