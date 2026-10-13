/*
 * Host-side unit tests for the fall-detection core.
 *
 * These run on the PC with no hardware, which is what makes the documented
 * "replay recorded motion and calibrate" workflow possible. Build and run with
 * host/build_and_test.cmd.
 *
 * The synthetic waveforms below are NOT validation data: they only prove that
 * the rule chain, the confirmation logic and the cancel window behave as
 * specified. Real trigger rates must come from recorded normal activity and
 * simulated falls (design document sections 8.2 and 19).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "attitude.h"
#include "fall_detector.h"
#include "imu_calib.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (cond) {                                         \
            g_pass++;                                       \
        }                                                   \
        else {                                              \
            g_fail++;                                       \
            printf("    FAIL %s:%d: ", __FILE__, __LINE__);  \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static int nearf(float a, float b, float tol)
{
    return fabsf(a - b) <= tol;
}

/* ------------------------------------------------------------------ */
/* scenario simulator                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    fall_detector_t det;
    attitude_t      att;
    uint64_t        t_us;
    float           hz;
} sim_t;

static void sim_init(sim_t *s, const fall_config_t *cfg)
{
    memset(s, 0, sizeof(*s));
    s->hz = (cfg != NULL && cfg->sample_hz_hint > 0.0f) ? cfg->sample_hz_hint : 125.0f;
    fall_detector_init(&s->det, cfg);
    attitude_init(&s->att, 0.98f);
}

/* MPU-6050 input: acceleration in g, angular rate in dps. */
static uint32_t sim_feed(sim_t *s, float ax, float ay, float az,
                         float gx, float gy, float gz)
{
    imu_sample_t smp;
    memset(&smp, 0, sizeof(smp));
    smp.t_us = s->t_us;
    smp.acc_mg.x = ax * 1000.0f;
    smp.acc_mg.y = ay * 1000.0f;
    smp.acc_mg.z = az * 1000.0f;
    smp.gyro_mdps.x = gx * 1000.0f;
    smp.gyro_mdps.y = gy * 1000.0f;
    smp.gyro_mdps.z = gz * 1000.0f;

    euler_t e;
    att_source_t src = attitude_update(&s->att, &smp, &e);
    uint32_t ev = fall_detector_update(&s->det, &smp, &e, src);
    s->t_us += (uint64_t)(1000000.0f / s->hz);
    return ev;
}

static uint32_t run_still(sim_t *s, float seconds, float roll_deg)
{

    /* Gravity in the body frame must follow the roll: at roll=90 deg the module
       lies on its side and the accelerometer reads gravity along +Y. Feeding a
       constant +Z vector while changing posture would be physically
       inconsistent, and the gravity-tilt criterion would then never move. */
    const float r = roll_deg * 0.017453292519943295f;
    const float ay = sinf(r);
    const float az = cosf(r);
    const uint32_t n = (uint32_t)(seconds * s->hz);
    uint32_t ev = 0;
    for (uint32_t i = 0; i < n; i++) {
        /* The events are returned: with the gravity-tilt criterion a fall is
           confirmed a little later, during the settle, so callers that check
           events must accumulate these too. */
        ev |= sim_feed(s, 0.03f, ay, az, 3.0f, -2.0f, 1.0f);
    }
    return ev;
}

/*
 * Documented simulated-fall shape: one acceleration impact, a posture change
 * that is fast but below the angular-rate impact threshold, then settling.
 * Returns the final roll angle.
 */
static float run_simulated_fall(sim_t *s)
{

    float roll = 0.0f;

    run_still(s, 1.0f, 0.0f);

    /* impact: acceleration spike, a few samples wide */
    for (int i = 0; i < 3; i++) {

        sim_feed(s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
    }

    /* rotate to lying: 90 deg over 500 ms -> 180 dps, gravity following the roll */
    const uint32_t steps = (uint32_t)(0.5f * s->hz);
    for (uint32_t i = 0; i < steps; i++) {
        roll += 90.0f / (float)steps;
        const float r = roll * 0.017453292519943295f;

        sim_feed(s, 0.03f, sinf(r), cosf(r), 180.0f, 0.0f, 0.0f);
    }

    /* settle, posture kept */
    run_still(s, 1.5f, roll);
    return roll;
}

static void banner(const char *name)
{
    printf("\n== %s\n", name);
}

/* ------------------------------------------------------------------ */

static void test_math(void)
{
    banner("attitude math");

    attitude_t att;
    attitude_init(&att, 0.98f);
    imu_sample_t sample = {0};
    sample.acc_mg.y = 500.0f;
    sample.acc_mg.z = 866.0254f;
    euler_t e;
    CHECK(attitude_update(&att, &sample, &e) == ATT_SRC_COMPLEMENTARY, "MPU attitude source");
    CHECK(nearf(e.roll_deg, 30.0f, 0.01f), "roll %.3f", e.roll_deg);
    CHECK(nearf(e.pitch_deg, 0.0f, 0.01f), "pitch %.3f", e.pitch_deg);
    CHECK(nearf(e.yaw_deg, 0.0f, 0.01f), "yaw %.3f", e.yaw_deg);
    sample.t_us = 8000;
    sample.gyro_mdps.z = 90000.0f;
    attitude_update(&att, &sample, &e);
    CHECK(nearf(e.yaw_deg, 0.72f, 0.01f), "gyro yaw %.3f", e.yaw_deg);

    euler_t eA = { 179.0f, 0.0f, 0.0f };
    euler_t eB = { -179.0f, 0.0f, 0.0f };
    CHECK(nearf(attitude_euler_delta_deg(&eA, &eB), 2.0f, 0.01f), "euler wrap");
}

static void test_normal_actions(void)
{
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    sim_t s;

    banner("normal: flat and still, 5 s");
    sim_init(&s, &cfg);
    run_still(&s, 5.0f, 0.0f);
    CHECK(fall_detector_stats(&s.det)->candidates == 0, "candidates %u",
          fall_detector_stats(&s.det)->candidates);
    CHECK(fall_detector_state(&s.det) == FALL_ST_MONITORING, "state");

    banner("normal: slow tilt to 90 deg and hold (tilt alone must not fire)");
    sim_init(&s, &cfg);
    {

        const uint32_t steps = (uint32_t)(5.0f * s.hz);
        for (uint32_t i = 0; i < steps; i++) {

            /* slow: ~18 dps, well below the angular-rate impact threshold */
            sim_feed(&s, 0.02f, -0.01f, 1.00f, 18.0f, 0.0f, 0.0f);
        }
        run_still(&s, 3.0f, 90.0f);
    }
    CHECK(fall_detector_stats(&s.det)->candidates == 0, "candidates %u",
          fall_detector_stats(&s.det)->candidates);
    CHECK(fall_detector_stats(&s.det)->suspected == 0, "suspected %u",
          fall_detector_stats(&s.det)->suspected);

    banner("normal: pick up and put down (moderate, sub-threshold motion)");
    sim_init(&s, &cfg);
    run_still(&s, 0.5f, 0.0f);
    for (int repeat = 0; repeat < 3; repeat++) {
        for (int i = 0; i < 20; i++) {
            sim_feed(&s, 0.10f, 0.05f, 1.75f, 90.0f, 20.0f, 10.0f);
        }
        run_still(&s, 1.0f, 0.0f);
    }
    CHECK(fall_detector_stats(&s.det)->candidates == 0, "candidates %u",
          fall_detector_stats(&s.det)->candidates);

    banner("normal: vigorous shaking, 3 s");
    sim_init(&s, &cfg);
    {

        for (int burst = 0; burst < 12; burst++) {
            /* acceleration spike ... */
            for (int i = 0; i < 2; i++) {

                sim_feed(&s, 0.80f, 0.60f, 2.90f, 260.0f, 40.0f, 10.0f);
            }
            /* ... followed by ordinary motion, not a settle */
            for (int i = 0; i < 8; i++) {

                sim_feed(&s, 0.35f, -0.30f, 1.20f, 150.0f, -80.0f, 30.0f);
            }
        }
    }
    CHECK(fall_detector_stats(&s.det)->suspected == 0, "suspected %u (shaking)",
          fall_detector_stats(&s.det)->suspected);
    CHECK(fall_detector_stats(&s.det)->rejected >= 1, "rejected %u",
          fall_detector_stats(&s.det)->rejected);
    printf("    shaking stats: candidates=%u rejected=%u repeated_impact=%u timeout=%u\n",
           fall_detector_stats(&s.det)->candidates,
           fall_detector_stats(&s.det)->rejected,
           fall_detector_stats(&s.det)->rejected_repeated_impact,
           fall_detector_stats(&s.det)->rejected_verify_timeout);
}

static void test_posture_timing(void)
{
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    sim_t s;

    /* The shipped default must stay untimed: the recorded run shows the posture
       time cannot separate the classes, so enabling it by default would cost
       recall for no gain. */
    CHECK(cfg.tilt_time_max_ms == 0, "default posture time limit is %u ms, expected 0 (off)",
          (unsigned)cfg.tilt_time_max_ms);

    /*
     * The differential test for the posture-time dimension: the same slow motion
     * must confirm with the time limit disabled and must NOT confirm with it
     * enabled. Without this pair the test could pass because of the motion, not
     * because of the new criterion.
     */
    banner("slow posture change (45 deg over 3 s) with no time limit: confirms");
    cfg.tilt_time_max_ms = 0;
    sim_init(&s, &cfg);
    {

        float roll = 0.0f;
        run_still(&s, 1.0f, 0.0f);
        for (int i = 0; i < 3; i++) {

            sim_feed(&s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
        }
        const uint32_t steps = (uint32_t)(3.0f * s.hz);
        for (uint32_t i = 0; i < steps; i++) {
            roll += 45.0f / (float)steps;          /* 15 deg/s: a deliberate lean */
            const float r = roll * 0.017453292519943295f;

            sim_feed(&s, 0.03f, sinf(r), cosf(r), 15.0f, 0.0f, 0.0f);
        }
        run_still(&s, 2.0f, roll);
    }
    CHECK(fall_detector_stats(&s.det)->suspected == 1, "untimed: suspected %u",
          fall_detector_stats(&s.det)->suspected);

    banner("the same slow posture change with a 1500 ms limit: rejected");
    fall_config_defaults(&cfg);
    cfg.tilt_time_max_ms = 1500;
    sim_init(&s, &cfg);
    {

        float roll = 0.0f;
        run_still(&s, 1.0f, 0.0f);
        for (int i = 0; i < 3; i++) {

            sim_feed(&s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
        }
        const uint32_t steps = (uint32_t)(3.0f * s.hz);
        for (uint32_t i = 0; i < steps; i++) {
            roll += 45.0f / (float)steps;
            const float r = roll * 0.017453292519943295f;

            sim_feed(&s, 0.03f, sinf(r), cosf(r), 15.0f, 0.0f, 0.0f);
        }
        run_still(&s, 2.0f, roll);
    }
    CHECK(fall_detector_stats(&s.det)->suspected == 0, "timed: suspected %u",
          fall_detector_stats(&s.det)->suspected);
    CHECK(fall_detector_stats(&s.det)->rejected_slow_posture >= 1,
          "timed: slow_posture rejections %u (no_attitude_change %u)",
          fall_detector_stats(&s.det)->rejected_slow_posture,
          fall_detector_stats(&s.det)->rejected_no_attitude_change);
    CHECK(fall_detector_state(&s.det) == FALL_ST_MONITORING, "timed: state %s",
          fall_state_name(fall_detector_state(&s.det)));

    banner("impact and no posture change at all: no_attitude_change, not slow_posture");
    fall_config_defaults(&cfg);
    sim_init(&s, &cfg);
    {

        run_still(&s, 1.0f, 0.0f);
        for (int i = 0; i < 3; i++) {
            sim_feed(&s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
        }
        run_still(&s, 5.0f, 0.0f);
    }
    CHECK(fall_detector_stats(&s.det)->suspected == 0, "flat after impact: suspected %u",
          fall_detector_stats(&s.det)->suspected);
    CHECK(fall_detector_stats(&s.det)->rejected_no_attitude_change >= 1,
          "flat after impact: no_attitude_change %u",
          fall_detector_stats(&s.det)->rejected_no_attitude_change);
    CHECK(fall_detector_stats(&s.det)->rejected_slow_posture == 0,
          "flat after impact: slow_posture %u (must be 0)",
          fall_detector_stats(&s.det)->rejected_slow_posture);

    banner("fast simulated fall with a 1500 ms limit: still confirms");
    fall_config_defaults(&cfg);
    cfg.tilt_time_max_ms = 1500;
    sim_init(&s, &cfg);
    {
        uint32_t ev_seen = 0;

        float roll = 0.0f;
        ev_seen |= run_still(&s, 1.0f, 0.0f);
        for (int i = 0; i < 3; i++) {

            ev_seen |= sim_feed(&s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
        }
        const uint32_t steps = (uint32_t)(0.5f * s.hz);   /* 90 deg over 500 ms */
        for (uint32_t i = 0; i < steps; i++) {
            roll += 90.0f / (float)steps;
            const float r = roll * 0.017453292519943295f;

            ev_seen |= sim_feed(&s, 0.03f, sinf(r), cosf(r), 180.0f, 0.0f, 0.0f);
        }
        ev_seen |= run_still(&s, 1.5f, roll);
        CHECK((ev_seen & FALL_EV_SUSPECTED) != 0, "fast fall: suspected event");
        printf("    posture change reached %u ms after the impact (limit %u ms)\n",
               fall_detector_last_tilt_time_ms(&s.det), (unsigned)cfg.tilt_time_max_ms);
    }
    CHECK(fall_detector_stats(&s.det)->suspected == 1, "fast fall: suspected %u",
          fall_detector_stats(&s.det)->suspected);
}

static void test_simulated_fall(void)
{
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    sim_t s;

    banner("simulated fall: impact -> posture change -> settle");
    sim_init(&s, &cfg);
    {
        uint32_t ev_seen = 0;

        float roll = 0.0f;

        ev_seen |= run_still(&s, 1.0f, 0.0f);
        for (int i = 0; i < 3; i++) {

            ev_seen |= sim_feed(&s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
        }
        const uint32_t steps = (uint32_t)(0.5f * s.hz);
        for (uint32_t i = 0; i < steps; i++) {
            roll += 90.0f / (float)steps;
            const float r = roll * 0.017453292519943295f;

            /* Gravity in the body frame follows the roll, as it must physically. */
            ev_seen |= sim_feed(&s, 0.03f, sinf(r), cosf(r), 180.0f, 0.0f, 0.0f);
        }
        ev_seen |= run_still(&s, 1.5f, roll);

        printf("    [confirming features] tilt=%.1f deg  euler delta=%.1f deg"
               "  energy=%.4f g2  |a|=%.3f g  quiet=%d\n",
               fall_detector_last_features(&s.det)->tilt_change_deg,
               fall_detector_last_features(&s.det)->att_change_deg,
               fall_detector_last_features(&s.det)->energy_g2,
               fall_detector_last_features(&s.det)->acc_mag_g,
               fall_detector_last_features(&s.det)->quiet ? 1 : 0);
        CHECK((ev_seen & FALL_EV_IMPACT_CANDIDATE) != 0, "impact candidate event");
        CHECK((ev_seen & FALL_EV_SUSPECTED) != 0, "suspected event");
        CHECK(fall_detector_state(&s.det) == FALL_ST_CANCEL_COUNTDOWN, "state %s",
              fall_state_name(fall_detector_state(&s.det)));
        CHECK(fall_detector_stats(&s.det)->suspected == 1, "suspected %u",
              fall_detector_stats(&s.det)->suspected);
        CHECK(fall_detector_cancel_deadline_us(&s.det) > 0, "deadline");
        printf("    features: |a|=%.3f g |w|=%.1f dps att_change=%.1f deg energy=%.4f g2\n",
               fall_detector_last_features(&s.det)->acc_mag_g,
               fall_detector_last_features(&s.det)->gyro_mag_dps,
               fall_detector_last_features(&s.det)->att_change_deg,
               fall_detector_last_features(&s.det)->energy_g2);
    }

    banner("simulated fall + cancel inside the window");
    sim_init(&s, &cfg);
    (void)run_simulated_fall(&s);
    CHECK(fall_detector_state(&s.det) == FALL_ST_CANCEL_COUNTDOWN, "countdown");
    {
        fall_reject_reason_t why = FALL_REJECT_NONE;
        const bool ok = fall_detector_cancel(&s.det, s.t_us, FALL_CANCEL_USER_KEY, &why);
        CHECK(ok, "cancel accepted");
        CHECK(fall_detector_state(&s.det) == FALL_ST_MONITORING, "back to monitoring");
        CHECK(fall_detector_stats(&s.det)->cancelled == 1, "cancelled %u",
              fall_detector_stats(&s.det)->cancelled);
        CHECK(fall_detector_last_cancel(&s.det) == FALL_CANCEL_USER_KEY, "cancel reason");
    }

    /* the same motion must not retrigger while the refractory window holds */
    {
        const uint32_t before = fall_detector_stats(&s.det)->candidates;

        for (int i = 0; i < 3; i++) {
            sim_feed(&s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
        }
        CHECK(fall_detector_stats(&s.det)->candidates == before,
              "candidate during refractory (%u -> %u)", before,
              fall_detector_stats(&s.det)->candidates);
        CHECK(fall_detector_stats(&s.det)->rejected_refractory >= 1, "refractory count");
    }

    banner("simulated fall left alone: window expires -> help pending");
    sim_init(&s, &cfg);
    (void)run_simulated_fall(&s);
    {
        const uint64_t since = fall_detector_state_since_us(&s.det);
        uint32_t ev = fall_detector_tick(&s.det, since + 14000 * 1000ULL);
        CHECK(ev == FALL_EV_NONE, "no event before the deadline");
        CHECK(fall_detector_state(&s.det) == FALL_ST_CANCEL_COUNTDOWN, "still counting");
        ev = fall_detector_tick(&s.det, since + 15001 * 1000ULL);
        CHECK((ev & FALL_EV_WINDOW_EXPIRED) != 0, "window expired event");
        CHECK(fall_detector_state(&s.det) == FALL_ST_HELP_PENDING, "state %s",
              fall_state_name(fall_detector_state(&s.det)));
        CHECK(fall_detector_stats(&s.det)->expired == 1, "expired %u",
              fall_detector_stats(&s.det)->expired);
    }

    banner("impact without posture change must be rejected");
    sim_init(&s, &cfg);
    {

        run_still(&s, 0.5f, 0.0f);
        for (int i = 0; i < 3; i++) {
            sim_feed(&s, 0.30f, 0.20f, 3.60f, 120.0f, -60.0f, 40.0f);
        }
        run_still(&s, 3.5f, 0.0f);
        CHECK(fall_detector_stats(&s.det)->suspected == 0, "suspected %u",
              fall_detector_stats(&s.det)->suspected);
        CHECK(fall_detector_last_reject(&s.det) == FALL_REJECT_NO_ATTITUDE_CHANGE,
              "reject reason %s", fall_reject_name(fall_detector_last_reject(&s.det)));
        CHECK(fall_detector_state(&s.det) == FALL_ST_MONITORING, "monitoring");
    }
}

static void test_imu_health(void)
{
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    sim_t s;

    banner("IMU reported down: no automatic judgement");
    sim_init(&s, &cfg);
    fall_detector_set_imu_ok(&s.det, false, 0);
    (void)run_simulated_fall(&s);
    CHECK(fall_detector_stats(&s.det)->candidates == 0, "candidates %u",
          fall_detector_stats(&s.det)->candidates);
    CHECK(fall_detector_state(&s.det) == FALL_ST_MONITORING, "state");

    banner("IMU healthy again: detection resumes");
    fall_detector_set_imu_ok(&s.det, true, s.t_us);
    (void)run_simulated_fall(&s);
    CHECK(fall_detector_stats(&s.det)->suspected == 1, "suspected %u",
          fall_detector_stats(&s.det)->suspected);


}

static void test_determinism(void)
{
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    sim_t a, b;

    banner("replay determinism");
    sim_init(&a, &cfg);
    sim_init(&b, &cfg);
    (void)run_simulated_fall(&a);
    (void)run_simulated_fall(&b);
    CHECK(memcmp(fall_detector_stats(&a.det), fall_detector_stats(&b.det), sizeof(fall_stats_t)) == 0,
          "identical replays must produce identical statistics");
    CHECK(fall_detector_state(&a.det) == fall_detector_state(&b.det), "same state");
}

static void test_calibration(void)
{
    banner("static IMU calibration");

    imu_calib_t cal;
    imu_sample_t s;
    imu_calib_begin(&cal);

    /* At rest, with a known gyro bias and an accelerometer reading ~3.9% low. */
    uint64_t t = 0;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 200; i++) {
        s.t_us = t;
        t += 8000ULL;
        s.acc_mg.x = 130.0f;
        s.acc_mg.y = -600.0f;
        s.acc_mg.z = -740.0f;
        s.gyro_mdps.x = 3600.0f;
        s.gyro_mdps.y = 700.0f;
        s.gyro_mdps.z = 2000.0f;
        imu_calib_accumulate(&cal, &s);
    }
    CHECK(imu_calib_finish(&cal), "still window accepted");
    CHECK(nearf(cal.gyro_bias_mdps[0], 3600.0f, 2.0f), "gyro bias x = %.1f", cal.gyro_bias_mdps[0]);
    CHECK(nearf(cal.gyro_bias_mdps[2], 2000.0f, 2.0f), "gyro bias z = %.1f", cal.gyro_bias_mdps[2]);
    CHECK(cal.acc_scale > 1.02f, "scale lifts a low |a| (%.4f)", cal.acc_scale);
    CHECK(!cal.per_axis_acc_bias, "a tilted window uses scale only (tilt %.1f deg)", cal.acc_tilt_deg);

    imu_sample_t corrected = s;
    imu_calib_apply(&cal, &corrected);
    CHECK(nearf(corrected.gyro_mdps.x, 0.0f, 0.5f), "bias removed (x=%.2f)", corrected.gyro_mdps.x);
    CHECK(nearf(vec3f_norm_mg(&corrected.acc_mg), 1000.0f, 1.0f), "|a| corrected to %.1f mg",
          vec3f_norm_mg(&corrected.acc_mg));

    /* A flat window can remove the per-axis offsets instead of only scaling. */
    imu_calib_begin(&cal);
    for (int i = 0; i < 200; i++) {
        memset(&s, 0, sizeof(s));
        s.t_us = (uint64_t)i * 8000ULL;
        s.acc_mg.x = 60.0f;
        s.acc_mg.y = -40.0f;
        s.acc_mg.z = 1060.0f; /* |a| = 1062 mg, tilt 3.9 deg */
        s.gyro_mdps.z = 1000.0f;
        imu_calib_accumulate(&cal, &s);
    }
    CHECK(imu_calib_finish(&cal), "flat window accepted");
    CHECK(cal.per_axis_acc_bias, "per-axis offsets used (tilt %.1f deg)", cal.acc_tilt_deg);
    imu_sample_t flat = s;
    imu_calib_apply(&cal, &flat);
    CHECK(nearf(flat.acc_mg.x, 0.0f, 0.5f), "offset x removed (%.2f mg)", flat.acc_mg.x);
    CHECK(nearf(flat.acc_mg.y, 0.0f, 0.5f), "offset y removed (%.2f mg)", flat.acc_mg.y);
    CHECK(nearf(flat.acc_mg.z, 1000.0f, 1.0f), "gravity restored (%.1f mg)", flat.acc_mg.z);
    CHECK(nearf(vec3f_norm_mg(&flat.acc_mg), 1000.0f, 1.0f), "|a| = %.1f mg",
          vec3f_norm_mg(&flat.acc_mg));

    /* Swinging along one axis keeps |w| constant - it must still be rejected. */
    imu_calib_begin(&cal);
    for (int i = 0; i < 200; i++) {
        memset(&s, 0, sizeof(s));
        s.t_us = (uint64_t)i * 8000ULL;
        s.acc_mg.z = 1000.0f;
        s.gyro_mdps.x = ((i % 2) != 0) ? 50000.0f : -50000.0f;
        imu_calib_accumulate(&cal, &s);
    }
    CHECK(!imu_calib_finish(&cal), "swinging window rejected (per-axis spread %.1f dps)",
          cal.gyro_spread_dps);
    CHECK(!cal.valid, "a rejected calibration is not applied");

    /* Too few samples is also a refusal. */
    imu_calib_begin(&cal);
    for (int i = 0; i < 5; i++) {
        memset(&s, 0, sizeof(s));
        s.acc_mg.z = 1000.0f;
        imu_calib_accumulate(&cal, &s);
    }
    CHECK(!imu_calib_finish(&cal), "short window rejected");
}

static void test_yaw_is_not_posture(void)
{
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    sim_t s;

    banner("a turn on the spot (yaw only) must not confirm a fall");
    {
        fall_config_t fast = cfg;
        fast.gyro_impact_dps = 120.0f; /* the spin is a candidate by design */
        sim_init(&s, &fast);

        run_still(&s, 0.5f, 0.0f);
        const uint32_t steps = (uint32_t)(1.2f * s.hz);
        for (uint32_t i = 0; i < steps; i++) {
            sim_feed(&s, 0.03f, 0.0f, 1.00f, 0.0f, 0.0f, 300.0f);
        }
        run_still(&s, 3.5f, 0.0f);

        CHECK(fall_detector_stats(&s.det)->candidates >= 1, "the spin raises a candidate");
        CHECK(fall_detector_stats(&s.det)->suspected == 0,
              "but a full turn never confirms (suspected=%u)",
              fall_detector_stats(&s.det)->suspected);
        CHECK(fall_detector_stats(&s.det)->rejected_no_attitude_change >= 1,
              "it is rejected as 'no posture change'");
        CHECK(fall_detector_state(&s.det) == FALL_ST_MONITORING, "back to monitoring");
    }
}

static void test_immediate_help(void)
{
    banner("immediate help and rearm do not repeat a settled event");
    fall_config_t cfg;
    fall_config_defaults(&cfg);
    cfg.cancel_window_ms = 0;
    sim_t s;
    sim_init(&s, &cfg);
    (void)run_simulated_fall(&s);
    CHECK(fall_detector_stats(&s.det)->suspected == 1, "one suspected event");
    CHECK(fall_detector_state(&s.det) == FALL_ST_HELP_PENDING, "no 15 second wait");
    fall_detector_rearm(&s.det, s.t_us);
    run_still(&s, 6.0f, 90.0f);
    CHECK(fall_detector_stats(&s.det)->suspected == 1, "settled event is not repeated");
    CHECK(fall_detector_state(&s.det) == FALL_ST_MONITORING, "detection keeps monitoring");
}

int main(void)
{
    printf("fall-core host tests\n");

    test_math();
    test_normal_actions();
    test_posture_timing();
    test_simulated_fall();
    test_yaw_is_not_posture();
    test_imu_health();
    test_calibration();
    test_determinism();
    test_immediate_help();

    printf("\n----------------------------------------\n");
    printf("checks passed: %d, failed: %d\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
