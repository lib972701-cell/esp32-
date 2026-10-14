/* MPU-6050 / GY-521: raw data-ready polling, complementary attitude
 * estimation, relative yaw, and 125 Hz default sampling. */
#ifndef IMU_MPU6050_H
#define IMU_MPU6050_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "imu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*imu_mpu6050_cb_t)(const imu_sample_t *sample, void *arg);

esp_err_t imu_mpu6050_init(void);
bool      imu_mpu6050_present(void);
esp_err_t imu_mpu6050_check_alive(void);
int       imu_mpu6050_read(imu_mpu6050_cb_t cb, void *arg);

uint64_t imu_mpu6050_now_us(void);

#ifdef __cplusplus
}
#endif

#endif /* IMU_MPU6050_H */
