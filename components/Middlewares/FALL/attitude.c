#include "attitude.h"

#include <math.h>
#include <string.h>

#define RAD_TO_DEG 57.29577951308232f
#define DEG_TO_RAD 0.017453292519943295f

/* Wrap an angle into (-180, 180]. */
static float wrap180(float deg)
{
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg <= -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void attitude_init(attitude_t *a, float alpha)
{
    if (a == NULL) {
        return;
    }
    memset(a, 0, sizeof(*a));
    a->alpha = clampf_local(alpha, 0.0f, 1.0f);
}

void attitude_reset(attitude_t *a)
{
    float alpha = (a != NULL) ? a->alpha : 0.98f;
    attitude_init(a, alpha);
}

float attitude_euler_delta_deg(const euler_t *a, const euler_t *b)
{
    if ((a == NULL) || (b == NULL)) {
        return 0.0f;
    }
    const float dr = wrap180(b->roll_deg - a->roll_deg);
    const float dp = wrap180(b->pitch_deg - a->pitch_deg);
    const float dy = wrap180(b->yaw_deg - a->yaw_deg);
    return sqrtf(dr * dr + dp * dp + dy * dy);
}

att_source_t attitude_update(attitude_t *a, const imu_sample_t *s, euler_t *out)
{
    if ((a == NULL) || (s == NULL)) {
        return ATT_SRC_NONE;
    }

    /* Sample interval, guarded against duplicate or out-of-order timestamps. */
    float dt = 0.0f;
    if (a->initialized && (s->t_us > a->last_t_us)) {
        dt = (float)(s->t_us - a->last_t_us) * 1e-6f;
        if (dt > 0.5f) {
            dt = 0.5f; /* a long gap is not a valid integration step */
        }
    }

    if (vec3f_norm_mg(&s->acc_mg) > 1.0f) {
        /*
         * Complementary filter. Accelerometer gives a drift-free but noisy
         * roll/pitch; the gyro is integrated for the fast component.
         * Yaw cannot be observed without a magnetometer, so it is pure
         * integration of the Z rate and therefore relative and drifting.
         */
        const float ax = s->acc_mg.x, ay = s->acc_mg.y, az = s->acc_mg.z;
        const float acc_roll = atan2f(ay, az) * RAD_TO_DEG;
        const float acc_pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;

        const float gx = (s->gyro_mdps.x * 1e-3f) * DEG_TO_RAD;
        const float gy = (s->gyro_mdps.y * 1e-3f) * DEG_TO_RAD;
        const float gz = (s->gyro_mdps.z * 1e-3f) * DEG_TO_RAD;

        if (!a->initialized) {
            a->euler.roll_deg = acc_roll;
            a->euler.pitch_deg = acc_pitch;
            a->euler.yaw_deg = 0.0f;
        }
        else {
            a->euler.roll_deg = a->alpha * (a->euler.roll_deg + gx * dt * RAD_TO_DEG) +
                                (1.0f - a->alpha) * acc_roll;
            a->euler.pitch_deg = a->alpha * (a->euler.pitch_deg + gy * dt * RAD_TO_DEG) +
                                 (1.0f - a->alpha) * acc_pitch;
            a->euler.yaw_deg = wrap180(a->euler.yaw_deg + gz * dt * RAD_TO_DEG);
        }
        a->euler.roll_deg = wrap180(a->euler.roll_deg);
        a->euler.pitch_deg = wrap180(a->euler.pitch_deg);
        a->source = ATT_SRC_COMPLEMENTARY;
        a->comp_frames++;
    }
    else {
        /* Free fall without a gravity reference: refuse to invent an attitude. */
        a->dropped_frames++;
        a->source = ATT_SRC_NONE;
        a->last_t_us = s->t_us;
        return ATT_SRC_NONE;
    }

    a->initialized = true;
    a->last_t_us = s->t_us;
    a->dt_s = dt;
    if (out != NULL) {
        *out = a->euler;
    }
    return a->source;
}
