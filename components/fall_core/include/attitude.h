/* MPU-6050 attitude estimation with a host complementary filter.
 * No magnetometer: yaw is relative and slowly drifts. */
#ifndef ATTITUDE_H
#define ATTITUDE_H

#include "fall_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Euler angles in degrees, ZYX (yaw-pitch-roll) convention. */
typedef struct {
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
} euler_t;

typedef enum {
    ATT_SRC_NONE = 0,       /* no usable source for this sample */
    ATT_SRC_COMPLEMENTARY = 2 /* Preserve the recorded CSV source value. */
} att_source_t;

typedef struct {
    float        alpha;        /* gyro weight of the complementary filter, 0..1 */
    float        dt_s;         /* last accepted sample interval */
    bool         initialized;
    euler_t      euler;
    att_source_t source;
    uint64_t     last_t_us;
    uint32_t     comp_frames;
    uint32_t     dropped_frames; /* no source available */
} attitude_t;

/* alpha = 0.98 is a conventional starting point at 100-200 Hz. */
void attitude_init(attitude_t *a, float alpha);
void attitude_reset(attitude_t *a);

/*
 * Feed one sample. Returns the source used for this sample and, when the
 * result is not ATT_SRC_NONE, fills *out.
 */
att_source_t attitude_update(attitude_t *a, const imu_sample_t *s, euler_t *out);

/* Shortest angular difference between two Euler triples, in degrees. */
float attitude_euler_delta_deg(const euler_t *a, const euler_t *b);


#ifdef __cplusplus
}
#endif

#endif /* ATTITUDE_H */
