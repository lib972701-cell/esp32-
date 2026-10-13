/*
 * Shared runtime state of the application.
 *
 * The firmware is split by concern - event handling, calibration, telemetry, the
 * sampling task - and all of those operate on the same detector, attitude filter
 * and calibration. The runtime owns a static instance and passes it explicitly to the sampling
 * task and event handler. Calibration and telemetry receive only their needed state.
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>

#include "attitude.h"
#include "fall_detector.h"
#include "fall_types.h"
#include "imu_calib.h"

typedef struct {
    fall_detector_t det;                /* the rule state machine               */
    attitude_t      att;                /* complementary attitude filter   */
    imu_calib_t     calib;              /* static calibration applied per sample */
    uint32_t        event_seq;          /* 1-based count of suspected falls     */
} app_state_t;


#endif /* APP_STATE_H */
