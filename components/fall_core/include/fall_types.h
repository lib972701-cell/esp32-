/*
 * Shared value types for the fall-detection core.
 *
 * This header is deliberately free of ESP-IDF dependencies so that the same
 * sources can be compiled for the target and for host-side replay / unit tests.
 */
#ifndef FALL_TYPES_H
#define FALL_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Right-handed 3-vector. Units are carried by the field name, not the type. */
typedef struct {
    float x;
    float y;
    float z;
} vec3f_t;

/* One MPU-6050 sample: acceleration in milli-g, angular rate in milli-dps. */
typedef struct {
    uint64_t t_us;          /* device timestamp, microseconds */
    vec3f_t  acc_mg;        /* acceleration, milli-g */
    vec3f_t  gyro_mdps;     /* angular rate, milli-dps */
    float    temperature_c;
    bool     temperature_valid;
} imu_sample_t;

/* Magnitude of an acceleration vector, in milli-g. */
static inline float vec3f_norm_mg(const vec3f_t *v)
{
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

/* Magnitude of an angular-rate vector, in milli-dps. */
static inline float vec3f_norm_mdps(const vec3f_t *v)
{
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

#ifdef __cplusplus
}
#endif

#endif /* FALL_TYPES_H */
