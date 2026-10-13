/*
 * Static IMU calibration.
 *
 * At rest the gyroscope reports its zero-rate offset and the accelerometer
 * reports gravity. The MPU-6050 has no on-sensor bias compensation, so the host
 * must estimate them: without this the
 * gyro offset is integrated straight into yaw (-2 deg/s was measured on the
 * bench unit) and a low acceleration magnitude biases the fall detector's quiet
 * and energy features.
 *
 * Only a device that is genuinely still is accepted: if the spread during the
 * window is too large the calibration is rejected rather than silently wrong.
 *
 * Pure C, no ESP-IDF dependency, so the host tests cover it too.
 *
 * Scope: gyro bias (3 axes) and a single accelerometer magnitude scale. Correct
 * per-axis accelerometer offsets and axis misalignment need a multi-position
 * calibration, which is deliberately not faked here - see acc_bias_mg.
 */
#ifndef IMU_CALIB_H
#define IMU_CALIB_H

#include <stdbool.h>
#include <stdint.h>

#include "fall_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A window that moved this much is not a valid rest measurement. */
#define IMU_CALIB_MAX_GYRO_SPREAD_DPS 3.0f
#define IMU_CALIB_MAX_ACC_SPREAD_G    0.120f
#define IMU_CALIB_MIN_SAMPLES         32

/*
 * Within this tilt of flat (Z up, component side up on a GY-521) the true
 * gravity vector is assumed to be (0, 0, +1 g), which lets the window remove
 * the per-axis accelerometer offsets instead of only scaling the magnitude.
 * Beyond it only the magnitude is corrected, because per-axis offsets need
 * multi-position data.
 */
#define IMU_CALIB_MAX_FLAT_TILT_DEG   20.0f

typedef struct {
    /* results */
    float    gyro_bias_mdps[3];
    float    acc_scale;        /* multiplies every accelerometer axis        */
    float    acc_bias_mg[3];   /* left at zero: needs multi-position data    */
    float    acc_norm_mean_mg; /* mean |a| observed before correction        */
    float    acc_tilt_deg;     /* mean tilt from flat during the window      */
    float    gyro_spread_dps;
    float    acc_spread_g;
    bool     per_axis_acc_bias;/* true when offsets (not just scale) were used */
    uint32_t samples;
    bool     valid;

    /* accumulators (kept here so the API is allocation-free and reentrant) */
    double   sum_gyro[3];
    double   sum_gyro_sq[3];
    double   sum_acc[3];
    double   sum_acc_sq[3];
    double   sum_acc_norm;
    double   sum_acc_norm_sq;
    double   sum_gyro_norm;
    double   sum_gyro_norm_sq;
} imu_calib_t;

void imu_calib_begin(imu_calib_t *c);
void imu_calib_accumulate(imu_calib_t *c, const imu_sample_t *s);

/* Decide from what was accumulated. Returns true when the result is usable. */
bool imu_calib_finish(imu_calib_t *c);

/* Apply the correction in place (no-op while the calibration is invalid). */
void imu_calib_apply(const imu_calib_t *c, imu_sample_t *s);

#ifdef __cplusplus
}
#endif

#endif /* IMU_CALIB_H */
