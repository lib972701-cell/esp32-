#include "fall_detector.h"

#include <math.h>
#include <string.h>

#define US_PER_MS 1000ULL
#define MS(x) ((uint64_t)(x) * US_PER_MS)

void fall_config_defaults(fall_config_t *cfg)
{
    /*
     * Conservative starting point, deliberately not the shipped tuning: the
     * firmware overwrites these from Kconfig in main/app_main.c, which is the
     * single source of truth for the product (currently 2.5 g / 120 dps / 30 deg,
     * bench-calibrated - see the Kconfig help text for the measured trade-off).
     * Host tools therefore print these values unless the thresholds are passed
     * explicitly, which tools/calibrate_from_recording.py always does.
     */
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));

    /*
     * Starting points only. The design document requires these to be
     * calibrated by replaying recorded normal activity and simulated falls;
     * they are not measured performance.
     */
    cfg->sample_hz_hint       = 125.0f; /* MPU-6050: 1 kHz / (1 + 7). */
    cfg->acc_impact_g         = 2.5f;
    cfg->gyro_impact_dps      = 300.0f;
    cfg->tilt_change_deg     = 35.0f;
    /*
     * The posture change has to happen quickly after the impact.
     *
     * DEFAULT 0 (off) is a measured decision, not an omission: replaying the
     * recorded 47-trial run shows the posture-change TIME does not separate the
     * two classes at all. The 14 confirmed hand-held "falls" reached their tilt
     * in 960-2976 ms (median 1632), while the 3 false-triggering normal actions
     * took 1344-2064 ms (median 1968) - the normal actions sit inside the falls'
     * range, and their tilt change is even larger (up to 69.7 deg against 59.9).
     * A limit tight enough to remove the false triggers (< 1344 ms) also removes
     * five of the fourteen falls. It measures how fast the operator moved a hand,
     * not the physics of a fall.
     *
     * The limit is kept because a real fall does complete its posture change in a
     * few hundred milliseconds, so this is the right shape for a body-worn trial -
     * but that has to be calibrated with real fall data, and enabling it on the
     * strength of this dataset would trade recall for nothing.
     */
    cfg->tilt_time_max_ms     = 0;
    cfg->quiet_acc_band_g     = 0.30f;
    cfg->quiet_gyro_dps       = 60.0f;
    cfg->quiet_min_ms         = 400;
    cfg->max_impacts_in_verify = 3;     /* one dominant impact is expected    */
    cfg->require_quiet_phase  = false;  /* soft falls must still confirm      */
    cfg->settle_energy_g2     = 0.05f;  /* weaker "motion settled" criterion  */
    cfg->verify_window_ms     = 3000;
    cfg->cancel_window_ms     = 15000;
    cfg->refractory_ms        = 5000;
    cfg->reject_refractory_ms = 1000;
    cfg->energy_window_ms     = 200;
    cfg->ring_ms              = 5000;
}

static uint16_t ring_size_for(const fall_config_t *cfg)
{
    float n = cfg->sample_hz_hint * ((float)cfg->ring_ms / 1000.0f);
    if (n < 16.0f) {
        n = 16.0f;
    }
    if (n > (float)FALL_RING_MAX) {
        n = (float)FALL_RING_MAX;
    }
    return (uint16_t)n;
}

static void ring_push(fall_detector_t *d, const fall_features_t *f, uint64_t t_us)
{
    d->ring[d->ring_head] = *f;
    d->ring_t_us[d->ring_head] = t_us;
    d->ring_head = (uint16_t)((d->ring_head + 1) % d->ring_size);
    if (d->ring_count < d->ring_size) {
        d->ring_count++;
    }
}

/* Mean of (|a| - 1g)^2 over the trailing window: a cheap motion-energy proxy. */
static float short_time_energy(const fall_detector_t *d, uint64_t now_us)
{
    const uint64_t oldest = (now_us > MS(d->cfg.energy_window_ms))
                                ? (now_us - MS(d->cfg.energy_window_ms))
                                : 0ULL;
    float sum = 0.0f;
    uint32_t n = 0;

    for (uint32_t i = 0; i < d->ring_count; i++) {
        const uint16_t idx = (uint16_t)((d->ring_head + d->ring_size - 1 - i) % d->ring_size);
        if (d->ring_t_us[idx] < oldest) {
            break;
        }
        const float e = d->ring[idx].acc_mag_g - 1.0f;
        sum += e * e;
        n++;
    }
    return (n > 0) ? (sum / (float)n) : 0.0f;
}

static void enter_state(fall_detector_t *d, fall_state_t st, uint64_t now_us)
{
    d->state = st;
    d->state_since_us = now_us;
}

/*
 * Reject the current candidate and return to monitoring.
 * Counting distinct impacts is a cheap use of the "impact timing" feature: a
 * fall is one dominant impact followed by settling, while ongoing vigorous
 * motion such as shaking produces many.
 */
static void reject_candidate(fall_detector_t *d, uint64_t now_us,
                             fall_reject_reason_t reason)
{
    d->last_reject = reason;
    d->stats.rejected++;
    switch (reason) {
    case FALL_REJECT_NO_ATTITUDE_CHANGE: d->stats.rejected_no_attitude_change++; break;
    case FALL_REJECT_SLOW_POSTURE:       d->stats.rejected_slow_posture++;       break;
    case FALL_REJECT_NO_QUIET_PHASE:     d->stats.rejected_no_quiet_phase++;     break;
    case FALL_REJECT_REPEATED_IMPACT:    d->stats.rejected_repeated_impact++;    break;
    case FALL_REJECT_VERIFY_TIMEOUT:     d->stats.rejected_verify_timeout++;     break;
    default: break;
    }
    d->refractory_until_us = now_us + MS(d->cfg.reject_refractory_ms);
    enter_state(d, FALL_ST_MONITORING, now_us);
}

static uint32_t handle_timeouts(fall_detector_t *d, uint64_t now_us)
{
    uint32_t ev = FALL_EV_NONE;

    switch (d->state) {
    case FALL_ST_SUSPECTED:
        if ((now_us - d->candidate_t_us) >= MS(d->cfg.verify_window_ms)) {
            fall_reject_reason_t why;
            if (d->max_tilt_change_deg < d->cfg.tilt_change_deg) {
                why = FALL_REJECT_NO_ATTITUDE_CHANGE;
            }
            else if ((d->cfg.tilt_time_max_ms != 0) &&
                     (d->tilt_time_ms > d->cfg.tilt_time_max_ms)) {
                why = FALL_REJECT_SLOW_POSTURE;
            }
            else if (d->cfg.require_quiet_phase && !d->quiet_confirmed) {
                why = FALL_REJECT_NO_QUIET_PHASE;
            }
            else {
                why = FALL_REJECT_VERIFY_TIMEOUT;
            }
            reject_candidate(d, now_us, why);
            ev |= FALL_EV_CANDIDATE_REJECTED;
        }
        break;

    case FALL_ST_CANCEL_COUNTDOWN:
        if ((now_us - d->state_since_us) >= MS(d->cfg.cancel_window_ms)) {
            d->stats.expired++;
            enter_state(d, FALL_ST_HELP_PENDING, now_us);
            ev |= FALL_EV_WINDOW_EXPIRED;
        }
        break;

    default:
        break;
    }
    return ev;
}

void fall_detector_init(fall_detector_t *d, const fall_config_t *cfg)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    if (cfg != NULL) {
        d->cfg = *cfg;
    }
    else {
        fall_config_defaults(&d->cfg);
    }
    d->ring_size = ring_size_for(&d->cfg);
    /*
     * Gravity low-pass, ~0.25 s time constant. The coefficient is derived from the
     * configured sample rate so the filter behaves the same at 120 Hz or 125 Hz.
     */
    d->grav_alpha = 1.0f - expf(-1.0f / (0.25f * ((d->cfg.sample_hz_hint > 1.0f)
                                                 ? d->cfg.sample_hz_hint : 120.0f)));
    d->imu_ok = true;
    d->state = FALL_ST_MONITORING;
    d->last_reject = FALL_REJECT_NONE;
    d->last_cancel = FALL_CANCEL_NONE;
}

void fall_detector_reset(fall_detector_t *d)
{
    if (d == NULL) {
        return;
    }
    const fall_config_t cfg = d->cfg;
    fall_detector_init(d, &cfg);
}

uint32_t fall_detector_update(fall_detector_t *d, const imu_sample_t *s,
                              const euler_t *att, att_source_t src)
{
    if ((d == NULL) || (s == NULL)) {
        return FALL_EV_NONE;
    }

    uint32_t ev = FALL_EV_NONE;

    /* ---- features ---------------------------------------------------- */
    fall_features_t f;
    memset(&f, 0, sizeof(f));
    f.acc_mag_g = vec3f_norm_mg(&s->acc_mg) * 1e-3f;
    f.gyro_mag_dps = vec3f_norm_mdps(&s->gyro_mdps) * 1e-3f;
    f.attitude_valid = (src != ATT_SRC_NONE) && (att != NULL);

    if (f.attitude_valid) {
        if (d->attitude_seen) {
            f.att_delta_deg = attitude_euler_delta_deg(&d->last_euler, att);
        }
        d->last_euler = *att;
        d->attitude_seen = true;
    }
    else {
        d->stats.attitude_unavailable_samples++;
    }

    f.quiet = (fabsf(f.acc_mag_g - 1.0f) <= d->cfg.quiet_acc_band_g) &&
              (f.gyro_mag_dps <= d->cfg.quiet_gyro_dps);

    ring_push(d, &f, s->t_us);

    /* Energy needs the sample already in the ring. */
    f.energy_g2 = short_time_energy(d, s->t_us);

    /* ---- time-based transitions first --------------------------------- */
    ev |= handle_timeouts(d, s->t_us);

    const uint64_t now_us = s->t_us;

    /*
     * Gravity direction, low-passed. This is the posture reference the rule uses:
     * a fall changes the body's tilt relative to gravity, while turning on the
     * spot (yaw) does not change it at all. Yaw is excluded on purpose - without a
     * magnetometer it is unobservable and carried by gyro integration alone, so a
     * fast turn would otherwise satisfy a posture-change test. Measured on the
     * bench: the one false trigger was a turn (45.6 deg of yaw, 7.6 deg of tilt).
     */
    {
        const float an = vec3f_norm_mg(&s->acc_mg);
        if (an > 1.0f) {
            const float ux = s->acc_mg.x / an;
            const float uy = s->acc_mg.y / an;
            const float uz = s->acc_mg.z / an;
            if (!d->grav_valid) {
                d->grav_lp[0] = ux;
                d->grav_lp[1] = uy;
                d->grav_lp[2] = uz;
                d->grav_valid = true;
            }
            else {
                d->grav_lp[0] += (ux - d->grav_lp[0]) * d->grav_alpha;
                d->grav_lp[1] += (uy - d->grav_lp[1]) * d->grav_alpha;
                d->grav_lp[2] += (uz - d->grav_lp[2]) * d->grav_alpha;
                const float n = sqrtf((d->grav_lp[0] * d->grav_lp[0]) +
                                      (d->grav_lp[1] * d->grav_lp[1]) +
                                      (d->grav_lp[2] * d->grav_lp[2]));
                if (n > 1e-6f) {
                    d->grav_lp[0] /= n;
                    d->grav_lp[1] /= n;
                    d->grav_lp[2] /= n;
                }
            }
        }
    }

    /*
     * Posture change since the impact candidate: the angle between the gravity
     * direction now and at the candidate. Kept up to date while the countdown runs
     * too, so the feature that confirmed the event stays visible in logs and in
     * offline replays. The Euler delta is still reported, but only as
     * information - it includes yaw and must not decide anything.
     */
    if (((d->state == FALL_ST_SUSPECTED) || (d->state == FALL_ST_CANCEL_COUNTDOWN)) &&
        (d->candidate_t_us != 0) && d->grav_valid) {
        float dot = (d->grav_ref[0] * d->grav_lp[0]) + (d->grav_ref[1] * d->grav_lp[1]) +
                    (d->grav_ref[2] * d->grav_lp[2]);
        dot = (dot > 1.0f) ? 1.0f : ((dot < -1.0f) ? -1.0f : dot);
        const float tilt = acosf(dot) * 57.29577951308232f;
        if (tilt > d->max_tilt_change_deg) {
            d->max_tilt_change_deg = tilt;
        }
        f.tilt_change_deg = d->max_tilt_change_deg;

        /*
         * The first moment the tilt threshold is met, and how long it took. This
         * is what tilt_time_max_ms judges; it is recorded even when the limit is
         * disabled, so the log and the offline calibration always show the
         * posture-change time that was actually measured.
         */
        if ((d->tilt_reached_us == 0) && (d->candidate_t_us != 0) &&
            (d->max_tilt_change_deg >= d->cfg.tilt_change_deg)) {
            d->tilt_reached_us = now_us;
            d->tilt_time_ms = (uint32_t)((now_us - d->candidate_t_us) / 1000ULL);
        }

        if (f.attitude_valid) {
            const float change = attitude_euler_delta_deg(&d->candidate_euler, att);
            if (change > d->max_att_change_deg) {
                d->max_att_change_deg = change;
            }
            f.att_change_deg = d->max_att_change_deg;
        }
    }

    {
        const uint16_t idx = (uint16_t)((d->ring_head + d->ring_size - 1) % d->ring_size);
        d->ring[idx].energy_g2 = f.energy_g2;
        d->ring[idx].att_change_deg = f.att_change_deg;
    }
    d->last = f;
    d->stats.samples++;
    const bool impact = (f.acc_mag_g >= d->cfg.acc_impact_g) ||
                        (f.gyro_mag_dps >= d->cfg.gyro_impact_dps);
    const bool acc_impact = (f.acc_mag_g >= d->cfg.acc_impact_g);
    const bool acc_impact_edge = acc_impact && !d->acc_impact_prev;
    if (acc_impact) {
        d->stats.impacts++;
    }

    switch (d->state) {
    case FALL_ST_MONITORING:
        if (!d->imu_ok) {
            break; /* automatic judgement suspended */
        }
        if (now_us < d->refractory_until_us) {
            if (impact) {
                d->stats.rejected++;
                d->stats.rejected_refractory++;
                d->last_reject = FALL_REJECT_REFRACTORY;
            }
            break;
        }
        if (impact) {
            d->stats.candidates++;
            d->candidate_t_us = now_us;
            if (f.attitude_valid) {
                d->candidate_euler = *att;
            }
            d->max_att_change_deg = 0.0f;
            d->max_tilt_change_deg = 0.0f;
            d->tilt_reached_us = 0;
            d->tilt_time_ms = 0;
            if (d->grav_valid) {
                d->grav_ref[0] = d->grav_lp[0];
                d->grav_ref[1] = d->grav_lp[1];
                d->grav_ref[2] = d->grav_lp[2];
            }
            d->quiet_active = false;
            d->quiet_confirmed = false;
            d->quiet_since_us = 0;
            d->impacts_since_candidate = acc_impact_edge ? 1 : 0;
            enter_state(d, FALL_ST_SUSPECTED, now_us);
            ev |= FALL_EV_IMPACT_CANDIDATE;
        }
        break;

    case FALL_ST_SUSPECTED: {
        if (acc_impact_edge && (++d->impacts_since_candidate > d->cfg.max_impacts_in_verify)) {
            /* Ongoing vigorous motion, not one impact followed by a fall. */
            reject_candidate(d, now_us, FALL_REJECT_REPEATED_IMPACT);
            ev |= FALL_EV_CANDIDATE_REJECTED;
            break;
        }

        /* Low-motion stage after the impact. */
        if (f.quiet) {
            if (!d->quiet_active) {
                d->quiet_active = true;
                d->quiet_since_us = now_us;
            }
            if ((now_us - d->quiet_since_us) >= MS(d->cfg.quiet_min_ms)) {
                d->quiet_confirmed = true;
            }
        }
        else {
            d->quiet_active = false;
        }

        /*
         * Posture criterion, now with a time structure. It used to be only
         * "the gravity tilt has changed by more than the threshold", which has no
         * time structure at all: leaning the device over slowly also satisfies it,
         * and because the motion is slow the energy test is satisfied too. On the
         * recorded 47-trial run that was the measured false-trigger mechanism.
         *
         * So the tilt has to be reached, and reached within tilt_time_max_ms of
         * the impact candidate. tilt_time_max_ms = 0 restores the untimed rule.
         */
        const bool tilt_reached = (d->max_tilt_change_deg >= d->cfg.tilt_change_deg);
        const uint32_t limit_ms = d->cfg.tilt_time_max_ms;
        const bool tilt_in_time = tilt_reached &&
                                  ((limit_ms == 0) || (d->tilt_time_ms <= limit_ms));
        const bool posture_ok = tilt_in_time;

        /*
         * The low-motion stage is a supporting feature, not a hard requirement
         * for every fall (a soft fall may settle without a long quiet stage).
         * Instead of dropping it, a weaker "motion has settled" test is used
         * unless the strict variant is configured.
         */
        const bool settled = d->quiet_confirmed || (f.energy_g2 <= d->cfg.settle_energy_g2);
        const bool motion_ok = d->cfg.require_quiet_phase ? d->quiet_confirmed : settled;

        if (posture_ok && motion_ok) {
            d->stats.suspected++;
            enter_state(d, FALL_ST_CANCEL_COUNTDOWN, now_us);
            ev |= FALL_EV_SUSPECTED;
        }
        /*
         * A candidate whose posture change was too late is not rejected here: it
         * stays in SUSPECTED until the verify deadline, where handle_timeouts()
         * can tell the three cases apart (no tilt at all / tilt too late / motion
         * never settled). Rejecting at the posture deadline instead would always
         * report "no attitude change", because at that instant the late tilt has
         * usually not been reached yet.
         */
        break;
    }

    default:
        break;
    }

    d->acc_impact_prev = acc_impact;
    return ev;
}

uint32_t fall_detector_tick(fall_detector_t *d, uint64_t now_us)
{
    if (d == NULL) {
        return FALL_EV_NONE;
    }
    return handle_timeouts(d, now_us);
}

bool fall_detector_cancel(fall_detector_t *d, uint64_t now_us,
                          fall_cancel_reason_t source,
                          fall_reject_reason_t *reason)
{
    if (d == NULL) {
        return false;
    }
    if (d->state != FALL_ST_CANCEL_COUNTDOWN) {
        if (reason != NULL) {
            *reason = FALL_REJECT_NONE;
        }
        return false;
    }

    d->last_cancel = source;
    d->stats.cancelled++;
    d->refractory_until_us = now_us + MS(d->cfg.refractory_ms);
    d->quiet_active = false;
    d->quiet_confirmed = false;
    d->max_att_change_deg = 0.0f;
    d->max_tilt_change_deg = 0.0f;
    d->tilt_reached_us = 0;
    d->tilt_time_ms = 0;
    enter_state(d, FALL_ST_MONITORING, now_us);

    if (reason != NULL) {
        *reason = FALL_REJECT_NONE;
    }
    return true;
}

void fall_detector_set_imu_ok(fall_detector_t *d, bool ok, uint64_t now_us)
{
    if (d == NULL) {
        return;
    }
    d->imu_ok = ok;
    if (!ok) {
        /*
         * Automatic judgement stops, but a running countdown is deliberately
         * left alone: losing the IMU must not cancel the user's chance to be
         * helped, and must not block the notification path.
         */
        d->last_reject = FALL_REJECT_IMU_DOWN;
    }
    (void)now_us;
}


void fall_detector_help_result(fall_detector_t *d, bool submitted, bool success)
{
    if (d == NULL) {
        return;
    }
    if (submitted) {
        d->state = success ? FALL_ST_SENT_OK : FALL_ST_SEND_FAILED;
    }
    else {
        d->state = FALL_ST_SUBMITTED;
    }
}

void fall_detector_rearm(fall_detector_t *d, uint64_t now_us)
{
    if (d == NULL) {
        return;
    }
    d->refractory_until_us = now_us + MS(d->cfg.refractory_ms);
    d->quiet_active = false;
    d->quiet_confirmed = false;
    d->max_att_change_deg = 0.0f;
    d->max_tilt_change_deg = 0.0f;
    d->tilt_reached_us = 0;
    d->tilt_time_ms = 0;
    enter_state(d, FALL_ST_MONITORING, now_us);
}

fall_state_t fall_detector_state(const fall_detector_t *d)
{
    return (d != NULL) ? d->state : FALL_ST_MONITORING;
}

uint32_t fall_detector_last_tilt_time_ms(const fall_detector_t *d)
{
    return (d != NULL) ? d->tilt_time_ms : 0;
}

const fall_features_t *fall_detector_last_features(const fall_detector_t *d)
{
    return (d != NULL) ? &d->last : NULL;
}

const fall_stats_t *fall_detector_stats(const fall_detector_t *d)
{
    return (d != NULL) ? &d->stats : NULL;
}

fall_reject_reason_t fall_detector_last_reject(const fall_detector_t *d)
{
    return (d != NULL) ? d->last_reject : FALL_REJECT_NONE;
}

fall_cancel_reason_t fall_detector_last_cancel(const fall_detector_t *d)
{
    return (d != NULL) ? d->last_cancel : FALL_CANCEL_NONE;
}

uint64_t fall_detector_state_since_us(const fall_detector_t *d)
{
    return (d != NULL) ? d->state_since_us : 0ULL;
}

uint64_t fall_detector_cancel_deadline_us(const fall_detector_t *d)
{
    if ((d == NULL) || (d->state != FALL_ST_CANCEL_COUNTDOWN)) {
        return 0ULL;
    }
    return d->state_since_us + MS(d->cfg.cancel_window_ms);
}

const char *fall_state_name(fall_state_t s)
{
    switch (s) {
    case FALL_ST_MONITORING:       return "monitoring";
    case FALL_ST_SUSPECTED:        return "suspected";
    case FALL_ST_CANCEL_COUNTDOWN: return "cancel_countdown";
    case FALL_ST_HELP_PENDING:     return "help_pending";
    case FALL_ST_SUBMITTED:        return "submitted";
    case FALL_ST_SENT_OK:          return "sent_ok";
    case FALL_ST_SEND_FAILED:      return "send_failed";
    default:                       return "unknown";
    }
}

const char *fall_reject_name(fall_reject_reason_t r)
{
    switch (r) {
    case FALL_REJECT_NONE:                return "none";
    case FALL_REJECT_NO_ATTITUDE:         return "no_attitude";
    case FALL_REJECT_NO_ATTITUDE_CHANGE:  return "no_attitude_change";
    case FALL_REJECT_SLOW_POSTURE:        return "slow_posture";
    case FALL_REJECT_NO_QUIET_PHASE:      return "no_quiet_phase";
    case FALL_REJECT_VERIFY_TIMEOUT:      return "verify_timeout";
    case FALL_REJECT_REPEATED_IMPACT:     return "repeated_impact";
    case FALL_REJECT_REFRACTORY:          return "refractory";
    case FALL_REJECT_IMU_DOWN:            return "imu_down";
    default:                              return "unknown";
    }
}

const char *fall_cancel_name(fall_cancel_reason_t c)
{
    switch (c) {
    case FALL_CANCEL_NONE:     return "none";
    case FALL_CANCEL_USER_KEY: return "user_key";
    case FALL_CANCEL_APP:      return "app";
    default:                   return "unknown";
    }
}
