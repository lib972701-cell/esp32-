/*
 * Fall detection: features, rule state machine, confirmation and cancellation.
 *
 * Implements section 8 of the design document:
 *   - approximately 5 s IMU ring buffer of features
 *   - features: acceleration magnitude, angular-rate magnitude, attitude change,
 *     impact timing, short-time motion energy
 *   - baseline rule state machine: impact / fast attitude change candidate ->
 *     check the following abnormal posture and low-motion stage -> suspected event
 *   - a tilt threshold on its own never declares a fall
 *   - a suspected event is never a diagnosis
 *   - cancel window (default 15 s); cancelling returns to monitoring and the
 *     same motion must not retrigger immediately
 *
 * Pure C, no allocation, no ESP-IDF dependency: the same code runs on the
 * target and in the host-side replay harness.
 */
#ifndef FALL_DETECTOR_H
#define FALL_DETECTOR_H

#include "attitude.h"
#include "imu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FALL_RING_MAX 1024

/* States of the fall state machine (design document section 6). */
typedef enum {
    FALL_ST_MONITORING = 0,   /* monitoring                      */
    FALL_ST_SUSPECTED,        /* suspected, still verifying      */
    FALL_ST_CANCEL_COUNTDOWN, /* cancel countdown running        */
    FALL_ST_HELP_PENDING,     /* help event persisted, to send   */
    FALL_ST_SUBMITTED,        /* submitted to the channel        */
    FALL_ST_SENT_OK,          /* channel reported success        */
    FALL_ST_SEND_FAILED       /* failed, retry pending           */
} fall_state_t;

/* Events returned by fall_detector_update()/fall_detector_tick(). */
typedef enum {
    FALL_EV_NONE               = 0,
    FALL_EV_IMPACT_CANDIDATE   = 1u << 0,
    FALL_EV_SUSPECTED          = 1u << 1,
    FALL_EV_CANDIDATE_REJECTED = 1u << 2,
    FALL_EV_CANCEL_ACCEPTED    = 1u << 3,
    FALL_EV_WINDOW_EXPIRED     = 1u << 4,
    FALL_EV_IMU_UNAVAILABLE    = 1u << 5
} fall_event_t;

typedef enum {
    FALL_REJECT_NONE = 0,
    FALL_REJECT_NO_ATTITUDE,      /* attitude source missing */
    FALL_REJECT_NO_ATTITUDE_CHANGE,
    FALL_REJECT_SLOW_POSTURE,     /* the tilt happened, but far too late to be a fall */
    FALL_REJECT_NO_QUIET_PHASE,
    FALL_REJECT_VERIFY_TIMEOUT,
    FALL_REJECT_REPEATED_IMPACT,
    FALL_REJECT_REFRACTORY,
    FALL_REJECT_IMU_DOWN
} fall_reject_reason_t;

typedef enum {
    FALL_CANCEL_NONE = 0,
    FALL_CANCEL_USER_KEY,
    FALL_CANCEL_APP
} fall_cancel_reason_t;

/*
 * Tunables. Every threshold is a starting point that must be calibrated by
 * replaying recorded normal / simulated-fall data - never frozen by guesswork.
 * Acceleration thresholds are in g, angular rates in dps.
 */
typedef struct {
    float    sample_hz_hint;     /* only used to size the ring buffer          */
    float    acc_impact_g;       /* impact candidate: |a|                      */
    float    gyro_impact_dps;    /* impact candidate: |w|                      */
    float    tilt_change_deg;    /* required gravity-tilt change after impact  */
    /*
     * How quickly that tilt must be reached after the impact candidate.
     * Without this the criterion has no time structure at all: a slow tilt
     * ("lean the device over and hold it") also satisfies "tilt changed and the
     * motion settled", which was the measured false-trigger mechanism. 0 keeps
     * the old, untimed behaviour.
     */
    uint32_t tilt_time_max_ms;   /* 0 = no time limit                          */
    float    quiet_acc_band_g;   /* "low motion" band around 1 g               */
    float    quiet_gyro_dps;     /* "low motion" angular-rate limit            */
    uint32_t quiet_min_ms;       /* duration of the low-motion stage           */
    uint8_t  max_impacts_in_verify;/* distinct impacts allowed before rejecting */
    bool     require_quiet_phase;/* false: soft falls must still confirm       */
    float    settle_energy_g2;   /* weaker "motion settled" energy criterion   */
    uint32_t verify_window_ms;   /* impact -> confirmation deadline            */
    uint32_t cancel_window_ms;   /* suspected -> help deadline (15000)         */
    uint32_t refractory_ms;      /* ignore new candidates after cancel/end     */
    uint32_t reject_refractory_ms;/* shorter hold after a rejected candidate   */
    uint32_t energy_window_ms;   /* short-time energy window                   */
    uint32_t ring_ms;            /* feature ring depth (5000)                  */
} fall_config_t;

typedef struct {
    float acc_mag_g;        /* |a|, g                                             */
    float gyro_mag_dps;     /* |w|, dps                                           */
    float att_delta_deg;    /* attitude step since the previous sample            */
    float att_change_deg;   /* Euler change since the candidate        */
    float tilt_change_deg;  /* GRAVITY-tilt change since the candidate (criterion) */
    float energy_g2;        /* short-time energy of |a| around 1 g                */
    bool  quiet;            /* this sample counts as low motion                   */
    bool  attitude_valid;
} fall_features_t;

typedef struct {
    uint32_t samples;
    uint32_t impacts;
    uint32_t candidates;
    uint32_t suspected;
    uint32_t cancelled;
    uint32_t expired;
    uint32_t rejected;
    uint32_t rejected_no_attitude_change;
    uint32_t rejected_slow_posture;
    uint32_t rejected_no_quiet_phase;
    uint32_t rejected_verify_timeout;
    uint32_t rejected_repeated_impact;
    uint32_t rejected_refractory;
    uint32_t attitude_unavailable_samples;
} fall_stats_t;

typedef struct {
    fall_config_t    cfg;
    fall_features_t  ring[FALL_RING_MAX];
    uint64_t         ring_t_us[FALL_RING_MAX];
    uint16_t         ring_head;
    uint16_t         ring_count;
    uint16_t         ring_size;

    fall_state_t     state;
    uint64_t         state_since_us;
    uint64_t         last_t_us;
    bool             started;

    fall_features_t  last;
    euler_t          last_euler;
    bool             attitude_seen;

    /* candidate bookkeeping */
    uint64_t         candidate_t_us;
    euler_t          candidate_euler;
    float            max_att_change_deg;
    float            max_tilt_change_deg;
    /* When the tilt threshold was first reached after the candidate (0 = not
       yet). Together with candidate_t_us this is the posture-change time that
       tilt_time_max_ms judges. */
    uint64_t         tilt_reached_us;
    /* Time from the candidate to that crossing, in ms; reported for the log and
       for offline calibration even before anything is confirmed. */
    uint32_t         tilt_time_ms;
    /* low-passed gravity direction: the fall-relevant posture reference. Yaw is
       deliberately excluded - without a magnetometer it is unobservable and is
       carried by gyro integration alone, so a turn would look like a fall. */
    float            grav_lp[3];
    float            grav_ref[3];
    float            grav_alpha;
    bool             grav_valid;
    bool             quiet_active;
    uint64_t         quiet_since_us;
    bool             quiet_confirmed;
    uint16_t         impacts_since_candidate;
    bool             acc_impact_prev;

    uint64_t         refractory_until_us;
    fall_reject_reason_t last_reject;
    fall_cancel_reason_t last_cancel;

    bool             imu_ok;
    fall_stats_t     stats;
} fall_detector_t;

/* Documented defaults; see fall_config_defaults() in the .c for the values. */
void fall_config_defaults(fall_config_t *cfg);

void fall_detector_init(fall_detector_t *d, const fall_config_t *cfg);
void fall_detector_reset(fall_detector_t *d);

/*
 * Feed one sample. Returns a bitmask of fall_event_t.
 * `att` and `src` come from attitude_update(); pass src == ATT_SRC_NONE when no
 * attitude was available for this sample.
 */
uint32_t fall_detector_update(fall_detector_t *d, const imu_sample_t *s,
                              const euler_t *att, att_source_t src);

/* Advance time-based transitions without a new sample. Returns events. */
uint32_t fall_detector_tick(fall_detector_t *d, uint64_t now_us);

/*
 * Cancel a suspected/help-pending event. Returns true when the cancel was
 * accepted (a countdown was running). Fills *reason when it was not.
 */
bool fall_detector_cancel(fall_detector_t *d, uint64_t now_us,
                          fall_cancel_reason_t source,
                          fall_reject_reason_t *reason);

/* IMU health. When false no new candidates are started; a running countdown
 * is deliberately not aborted. */
void fall_detector_set_imu_ok(fall_detector_t *d, bool ok, uint64_t now_us);


/* Help channel result, driven by the application layer. */
void fall_detector_help_result(fall_detector_t *d, bool submitted, bool success);

/* Terminal state -> back to monitoring after the refractory window. */
void fall_detector_rearm(fall_detector_t *d, uint64_t now_us);

fall_state_t fall_detector_state(const fall_detector_t *d);
/* Time from the impact candidate to the moment the tilt threshold was reached,
   in ms (0 when it has not been reached). This is what tilt_time_max_ms judges,
   and it is worth logging: it shows how close a rejected motion came. */
uint32_t fall_detector_last_tilt_time_ms(const fall_detector_t *d);
const fall_features_t *fall_detector_last_features(const fall_detector_t *d);
const fall_stats_t *fall_detector_stats(const fall_detector_t *d);
fall_reject_reason_t fall_detector_last_reject(const fall_detector_t *d);
fall_cancel_reason_t fall_detector_last_cancel(const fall_detector_t *d);
uint64_t fall_detector_state_since_us(const fall_detector_t *d);
uint64_t fall_detector_cancel_deadline_us(const fall_detector_t *d);

const char *fall_state_name(fall_state_t s);
const char *fall_reject_name(fall_reject_reason_t r);
const char *fall_cancel_name(fall_cancel_reason_t c);

#ifdef __cplusplus
}
#endif

#endif /* FALL_DETECTOR_H */
