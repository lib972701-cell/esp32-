/*
 * Static IMU calibration at start-up.
 *
 * The device must be still for the window, otherwise the sample spread is too
 * large and the attempt is rejected rather than silently accepted. This module
 * only drives the attempts and reports the outcome; the estimator itself lives
 * in components/Middlewares/FALL/imu_calib.c so it can be unit tested on a PC.
 */
#ifndef APP_CALIB_H
#define APP_CALIB_H

#include "imu_calib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Try up to CONFIG_FALL_CALIB_TRIES still windows and log the result. */
void app_calib_run(imu_calib_t *calib);

#ifdef __cplusplus
}
#endif

#endif /* APP_CALIB_H */
