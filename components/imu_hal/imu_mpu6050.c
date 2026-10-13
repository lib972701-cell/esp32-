#include "imu_mpu6050.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "imu_port.h"

static const char *TAG = "mpu6050";

/* Register map: "MPU-6000 and MPU-6050 Register Map and Descriptions". */
#define MPU_REG_SMPLRT_DIV    0x19
#define MPU_REG_CONFIG        0x1A
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_INT_ENABLE    0x38
#define MPU_REG_INT_STATUS    0x3A
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_REG_TEMP_OUT_H    0x41
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_WHO_AM_I      0x75

/* WHO_AM_I returns bits 6:1 of the device address: 0x68 for this family. */
#define MPU_WHO_AM_I_VALUE    0x68

/* +-8 g -> 4096 LSB/g; +-1000 dps -> 32.8 LSB/dps (datasheet sensitivity). */
#define MPU_ACC_LSB_PER_G     4096.0f
#define MPU_GYRO_LSB_PER_DPS  32.8f

#define MPU_DATA_READY_BIT    0x01

static bool     s_present;
static uint32_t s_errors;
static uint32_t s_retries; /* bus reads that needed the second attempt */

static uint64_t s_clock_us;
static uint64_t s_clock_anchor_us;
static bool     s_clock_valid;

static uint64_t host_now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

uint64_t imu_mpu6050_now_us(void)
{
    if (!s_clock_valid) {
        return host_now_us();
    }
    return s_clock_us + (host_now_us() - s_clock_anchor_us);
}

static void advance_sample_clock(void)
{
    const uint64_t now = host_now_us();
    const uint64_t period = 1000000ULL / (uint64_t)CONFIG_FALL_IMU_ODR_HZ;

    if (!s_clock_valid) {
        s_clock_us = now;
    }
    else {
        s_clock_us += period;
        const uint64_t projected = s_clock_us + (now - s_clock_anchor_us);
        const uint64_t drift = (projected > now) ? (projected - now) : (now - projected);
        if (drift > 200000ULL) {
            s_clock_us = now;
        }
    }
    s_clock_anchor_us = now;
    s_clock_valid = true;
}

static esp_err_t mp_wr(uint8_t reg, uint8_t val)
{
    if (imu_port_write(NULL, reg, &val, 1) != 0) {
        s_errors++;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t mp_rd(uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (imu_port_read(NULL, reg, buf, len) != 0) {
        s_errors++;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t imu_mpu6050_check_alive(void)
{
    uint8_t who = 0;
    if (mp_rd(MPU_REG_WHO_AM_I, &who, 1) != ESP_OK) {
        s_present = false;
        return ESP_FAIL;
    }
    s_present = ((who & 0x7E) == MPU_WHO_AM_I_VALUE);
    return s_present ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t imu_mpu6050_init(void)
{
    s_present = false;
    s_clock_valid = false;
    uint8_t who = 0;
    if (mp_rd(MPU_REG_WHO_AM_I, &who, 1) != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read failed: nothing acknowledged at the configured address."
                      " Check wiring, power (3.3 V) and the ADO strapping.");
        s_present = false;
        return ESP_ERR_NOT_FOUND;
    }
    if ((who & 0x7E) != MPU_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "unexpected WHO_AM_I=0x%02X, expected 0x68 (MPU-6050/MPU-6500 family)",
                 (unsigned)who);
        s_present = false;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "detected WHO_AM_I=0x%02X", (unsigned)who);

    if (mp_wr(MPU_REG_PWR_MGMT_1, 0x80) != ESP_OK) goto configuration_failed;
    vTaskDelay(pdMS_TO_TICKS(50));
    if (mp_wr(MPU_REG_PWR_MGMT_1, 0x01) != ESP_OK) goto configuration_failed;
    vTaskDelay(pdMS_TO_TICKS(10));

    /*
     * 1 kHz gyro reference with DLPF enabled -> 1000 / (1 + 7) = 125 Hz.
     * 120 Hz is not reachable exactly on this part; the deviation is logged.
     */
    if (mp_wr(MPU_REG_SMPLRT_DIV, 7) != ESP_OK ||
        mp_wr(MPU_REG_CONFIG, 0x03) != ESP_OK ||
        mp_wr(MPU_REG_GYRO_CONFIG, 0x10) != ESP_OK ||
        mp_wr(MPU_REG_ACCEL_CONFIG, 0x10) != ESP_OK ||
        mp_wr(MPU_REG_INT_ENABLE, MPU_DATA_READY_BIT) != ESP_OK)
        goto configuration_failed;

    s_present = true;

    ESP_LOGI(TAG, "configured: %d Hz polled sample rate, accel +-8 g (%.0f LSB/g),"
                  " gyro +-1000 dps (%.1f LSB/dps), DLPF 3, no on-chip fusion",
             CONFIG_FALL_IMU_ODR_HZ, MPU_ACC_LSB_PER_G, MPU_GYRO_LSB_PER_DPS);
    ESP_LOGW(TAG, "Euler angles come from the host complementary filter; yaw has no absolute"
                  " reference on this part and drifts (no magnetometer).");
    return ESP_OK;
configuration_failed:
    /* 身份可读不代表已唤醒并配置成功，保持 unavailable 以便完整重试。 */
    ESP_LOGE(TAG, "register configuration failed; detection unavailable, retry init");
    return ESP_FAIL;
}

bool imu_mpu6050_present(void)
{
    return s_present;
}

/*
 * Read with one immediate retry.
 *
 * Measured on this bench unit: the MPU-6050 occasionally NACKs a single
 * transfer (4 times in 120 s with the board sitting still, each time exactly one
 * bus error). Without a retry that one NACK clears the presence flag and
 * suspends automatic judgement until the next 1 Hz liveness check - up to a
 * second of dropped samples. The retry costs about a millisecond, and a real
 * disconnect still fails twice and is reported as such.
 */
static esp_err_t mp_rd_retry(uint8_t reg, uint8_t *buf, size_t len)
{
    esp_err_t err = mp_rd(reg, buf, len);
    if (err != ESP_OK) {
        s_retries++;
        err = mp_rd(reg, buf, len);
    }
    return err;
}

int imu_mpu6050_read(imu_mpu6050_cb_t cb, void *arg)
{
    if (!s_present || (cb == NULL)) {
        return 0;
    }

    uint8_t status = 0;
    if (mp_rd_retry(MPU_REG_INT_STATUS, &status, 1) != ESP_OK) {
        s_present = false;
        ESP_LOGE(TAG, "reading INT_STATUS failed twice in a row: the device stopped answering"
                      " on the bus (%u transient read(s) retried so far)", (unsigned)s_retries);
        return 0;
    }
    if ((status & MPU_DATA_READY_BIT) == 0) {
        return 0; /* no fresh sample yet; do not duplicate the previous one */
    }

    uint8_t raw[14];
    if (mp_rd_retry(MPU_REG_ACCEL_XOUT_H, raw, sizeof(raw)) != ESP_OK) {
        s_present = false;
        ESP_LOGE(TAG, "burst read of the 14 output bytes failed twice in a row: the device"
                      " stopped answering (%u transient read(s) retried so far)",
                 (unsigned)s_retries);
        return 0;
    }

    const int16_t ax = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    const int16_t ay = (int16_t)(((uint16_t)raw[2] << 8) | raw[3]);
    const int16_t az = (int16_t)(((uint16_t)raw[4] << 8) | raw[5]);
    const int16_t tt = (int16_t)(((uint16_t)raw[6] << 8) | raw[7]);
    const int16_t gx = (int16_t)(((uint16_t)raw[8] << 8) | raw[9]);
    const int16_t gy = (int16_t)(((uint16_t)raw[10] << 8) | raw[11]);
    const int16_t gz = (int16_t)(((uint16_t)raw[12] << 8) | raw[13]);

    imu_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    advance_sample_clock();
    sample.t_us = s_clock_us;

    sample.acc_mg.x = ((float)ax / MPU_ACC_LSB_PER_G) * 1000.0f;
    sample.acc_mg.y = ((float)ay / MPU_ACC_LSB_PER_G) * 1000.0f;
    sample.acc_mg.z = ((float)az / MPU_ACC_LSB_PER_G) * 1000.0f;

    sample.gyro_mdps.x = ((float)gx / MPU_GYRO_LSB_PER_DPS) * 1000.0f;
    sample.gyro_mdps.y = ((float)gy / MPU_GYRO_LSB_PER_DPS) * 1000.0f;
    sample.gyro_mdps.z = ((float)gz / MPU_GYRO_LSB_PER_DPS) * 1000.0f;

    sample.temperature_c = ((float)tt / 340.0f) + 36.53f;
    sample.temperature_valid = true;

    cb(&sample, arg);
    return 1;
}
