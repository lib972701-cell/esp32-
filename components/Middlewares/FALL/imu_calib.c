#include "imu_calib.h"

#include <math.h>
#include <string.h>

void imu_calib_begin(imu_calib_t *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->acc_scale = 1.0f;
}

void imu_calib_accumulate(imu_calib_t *c, const imu_sample_t *s)
{
    if ((c == NULL) || (s == NULL)) {
        return;
    }

    c->sum_gyro[0] += s->gyro_mdps.x;
    c->sum_gyro[1] += s->gyro_mdps.y;
    c->sum_gyro[2] += s->gyro_mdps.z;
    c->sum_gyro_sq[0] += (double)s->gyro_mdps.x * s->gyro_mdps.x;
    c->sum_gyro_sq[1] += (double)s->gyro_mdps.y * s->gyro_mdps.y;
    c->sum_gyro_sq[2] += (double)s->gyro_mdps.z * s->gyro_mdps.z;
    c->sum_acc[0] += s->acc_mg.x;
    c->sum_acc[1] += s->acc_mg.y;
    c->sum_acc[2] += s->acc_mg.z;
    c->sum_acc_sq[0] += (double)s->acc_mg.x * s->acc_mg.x;
    c->sum_acc_sq[1] += (double)s->acc_mg.y * s->acc_mg.y;
    c->sum_acc_sq[2] += (double)s->acc_mg.z * s->acc_mg.z;

    const double acc_norm = (double)vec3f_norm_mg(&s->acc_mg);
    const double gyro_norm = (double)vec3f_norm_mdps(&s->gyro_mdps);
    c->sum_acc_norm += acc_norm;
    c->sum_acc_norm_sq += acc_norm * acc_norm;
    c->sum_gyro_norm += gyro_norm;
    c->sum_gyro_norm_sq += gyro_norm * gyro_norm;

    c->samples++;
}

static double spread(double sum, double sum_sq, uint32_t n)
{
    if (n < 2) {
        return 0.0;
    }
    const double mean = sum / (double)n;
    const double var = (sum_sq / (double)n) - (mean * mean);
    return (var > 0.0) ? sqrt(var) : 0.0;
}

bool imu_calib_finish(imu_calib_t *c)
{
    if (c == NULL) {
        return false;
    }
    const uint32_t n = c->samples;
    if (n < IMU_CALIB_MIN_SAMPLES) {
        c->valid = false;
        return false;
    }

    /*
     * Stillness is judged per axis, not on the vector magnitude: a device swung
     * back and forth along one axis keeps a constant |w| (and a constant |a| when
     * it rotates), so a magnitude test would wrongly accept it.
     */
    double gyro_spread_max = 0.0;
    double acc_spread_max = 0.0;
    for (int i = 0; i < 3; i++) {
        const double gs = spread(c->sum_gyro[i], c->sum_gyro_sq[i], n);
        const double as = spread(c->sum_acc[i], c->sum_acc_sq[i], n);
        if (gs > gyro_spread_max) {
            gyro_spread_max = gs;
        }
        if (as > acc_spread_max) {
            acc_spread_max = as;
        }
    }
    c->gyro_spread_dps = (float)(gyro_spread_max / 1000.0);
    c->acc_spread_g = (float)(acc_spread_max / 1000.0);
    c->acc_norm_mean_mg = (float)(c->sum_acc_norm / (double)n);

    c->gyro_bias_mdps[0] = (float)(c->sum_gyro[0] / (double)n);
    c->gyro_bias_mdps[1] = (float)(c->sum_gyro[1] / (double)n);
    c->gyro_bias_mdps[2] = (float)(c->sum_gyro[2] / (double)n);

    /*
     * A single scale factor can only correct the magnitude, not per-axis
     * offsets. When the window is at rest and close to flat, the true gravity
     * vector is (0, 0, +1 g), so the mean vector directly gives the offsets and
     * no scaling is needed. Otherwise fall back to the magnitude-only fix, and
     * refuse entirely if |a| is far from 1 g (that window was not at rest).
     */
    const double mx = c->sum_acc[0] / (double)n;
    const double my = c->sum_acc[1] / (double)n;
    const double mz = c->sum_acc[2] / (double)n;
    c->acc_tilt_deg = (float)(atan2(sqrt((mx * mx) + (my * my)), mz) * 57.29577951308232);

    const bool acc_plausible = (c->acc_norm_mean_mg > 500.0f) && (c->acc_norm_mean_mg < 2000.0f);
    const bool flat = fabsf(c->acc_tilt_deg) <= IMU_CALIB_MAX_FLAT_TILT_DEG;

    if (acc_plausible && flat) {
        c->acc_bias_mg[0] = (float)mx;
        c->acc_bias_mg[1] = (float)my;
        c->acc_bias_mg[2] = (float)(mz - 1000.0);
        c->acc_scale = 1.0f;
        c->per_axis_acc_bias = true;
    }
    else {
        c->acc_bias_mg[0] = 0.0f;
        c->acc_bias_mg[1] = 0.0f;
        c->acc_bias_mg[2] = 0.0f;
        c->acc_scale = acc_plausible ? (1000.0f / c->acc_norm_mean_mg) : 1.0f;
        c->per_axis_acc_bias = false;
    }

    c->valid = acc_plausible &&
               (c->gyro_spread_dps <= IMU_CALIB_MAX_GYRO_SPREAD_DPS) &&
               (c->acc_spread_g <= IMU_CALIB_MAX_ACC_SPREAD_G);
    return c->valid;
}

void imu_calib_apply(const imu_calib_t *c, imu_sample_t *s)
{
    if ((c == NULL) || (s == NULL) || !c->valid) {
        return;
    }

    s->gyro_mdps.x -= c->gyro_bias_mdps[0];
    s->gyro_mdps.y -= c->gyro_bias_mdps[1];
    s->gyro_mdps.z -= c->gyro_bias_mdps[2];

    s->acc_mg.x = (s->acc_mg.x - c->acc_bias_mg[0]) * c->acc_scale;
    s->acc_mg.y = (s->acc_mg.y - c->acc_bias_mg[1]) * c->acc_scale;
    s->acc_mg.z = (s->acc_mg.z - c->acc_bias_mg[2]) * c->acc_scale;
}
