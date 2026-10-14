#include "app_config.h"

#include <inttypes.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "fall_app";

void app_configure(app_state_t *app)
{
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    cfg.acc_impact_g          = (float)CONFIG_FALL_ACC_IMPACT_MG / 1000.0f;
    cfg.gyro_impact_dps       = (float)CONFIG_FALL_GYRO_IMPACT_MDPS / 1000.0f;
    cfg.tilt_change_deg       = (float)CONFIG_FALL_TILT_CHANGE_MDEG / 1000.0f;
    cfg.tilt_time_max_ms      = CONFIG_FALL_TILT_TIME_MAX_MS;
    cfg.quiet_acc_band_g      = (float)CONFIG_FALL_QUIET_BAND_MG / 1000.0f;
    cfg.quiet_gyro_dps        = (float)CONFIG_FALL_QUIET_GYRO_MDPS / 1000.0f;
    cfg.quiet_min_ms          = CONFIG_FALL_QUIET_MIN_MS;
    cfg.settle_energy_g2      = (float)CONFIG_FALL_SETTLE_ENERGY_UG2 / 1000000.0f;
#ifdef CONFIG_FALL_REQUIRE_QUIET_PHASE
    cfg.require_quiet_phase   = true;
#else
    /* Disabled bools are not emitted into sdkconfig.h at all. */
    cfg.require_quiet_phase   = false;
#endif
    cfg.verify_window_ms      = CONFIG_FALL_VERIFY_WINDOW_MS;
    cfg.max_impacts_in_verify = (uint8_t)CONFIG_FALL_MAX_IMPACTS;
    cfg.cancel_window_ms      = 0; /* 疑似摔倒成立后立即通知，无取消倒计时。 */
    cfg.refractory_ms         = CONFIG_FALL_REFRACTORY_MS;
    cfg.sample_hz_hint        = (float)CONFIG_FALL_IMU_ODR_HZ;

    fall_detector_init(&app->det, &cfg);
    attitude_init(&app->att, 0.98f);

    ESP_LOGI(TAG, "thresholds: |a|>=%.2f g, |w|>=%.1f dps, gravity tilt>=%.1f deg,"
                  " verify=%" PRIu32 " ms, cancel=%" PRIu32 " ms, max impacts=%u",
             cfg.acc_impact_g, cfg.gyro_impact_dps, cfg.tilt_change_deg,
             cfg.verify_window_ms, cfg.cancel_window_ms, (unsigned)cfg.max_impacts_in_verify);
    if (cfg.tilt_time_max_ms == 0) {
        ESP_LOGW(TAG, "posture-time limit is OFF: the recorded hand-held trials show the time a"
                      " motion takes to change posture does not separate falls from normal"
                      " actions (evidence/posture-timing.txt), so it stays available for a"
                      " body-worn trial instead of being enabled on that data");
    }
    else {
        ESP_LOGW(TAG, "posture change must be reached within %" PRIu32 " ms of the impact",
                 cfg.tilt_time_max_ms);
    }
    ESP_LOGI(TAG, "these values are bench-calibrated; the measured trade-off and the tool to"
                  " re-calibrate them are documented in main/Kconfig.projbuild");
}

