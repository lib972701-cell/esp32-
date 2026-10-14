/* MPU-6050 device interface used by the sampling pipeline. */
#ifndef IMU_DEVICE_H
#define IMU_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "imu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*imu_device_cb_t)(const imu_sample_t *sample, void *arg);

/* Configure the sensor. ESP_ERR_NOT_FOUND when the identity check fails. */
esp_err_t imu_device_init(void);

bool imu_device_present(void);

/* Re-read the identity register and update the presence flag. */
esp_err_t imu_device_check_alive(void);

/* Fetch available samples. Returns how many were delivered. */
int imu_device_read(imu_device_cb_t cb, void *arg);


/*
 * Single time base for the detector.
 *
 * Sample timestamps advance in exact ODR steps while samples flow and keep
 * tracking the host clock when they stop, so a cancel window still expires if
 * the sensor dies mid-countdown. Never mix this with esp_timer_get_time()
 * inside the detector: the windows would be compared against two clocks.
 */
uint64_t imu_device_now_us(void);

/* Human-readable name of the compiled backend, for the boot log. */
const char *imu_device_name(void);

#ifdef __cplusplus
}
#endif

#endif /* IMU_DEVICE_H */
