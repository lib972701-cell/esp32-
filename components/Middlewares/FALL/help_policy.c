#include "help_policy.h"

#include <string.h>

#define MS(ms) ((uint64_t)(ms) * 1000ULL)

static uint16_t clamp_capacity(uint16_t capacity)
{
    if (capacity == 0) {
        return 1;
    }
    if (capacity > HELP_QUEUE_MAX) {
        return HELP_QUEUE_MAX;
    }
    return capacity;
}

void help_policy_defaults(help_policy_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->retry_ms[0] = 5000;
    cfg->retry_ms[1] = 15000;
    cfg->retry_ms[2] = 30000;
    cfg->retry_ms[3] = 60000;
    cfg->retry_steps = 4;
    cfg->max_attempts = 5;
    cfg->capacity = HELP_QUEUE_MAX;
}

void help_queue_init(help_queue_t *q, const help_policy_cfg_t *cfg)
{
    if (q == NULL) {
        return;
    }
    memset(q, 0, sizeof(*q));
    if (cfg != NULL) {
        q->cfg = *cfg;
    }
    else {
        help_policy_defaults(&q->cfg);
    }
    q->cfg.capacity = clamp_capacity(q->cfg.capacity);
    if (q->cfg.max_attempts == 0) {
        q->cfg.max_attempts = 1;
    }
    if (q->cfg.retry_steps > HELP_RETRY_STEPS_MAX) {
        q->cfg.retry_steps = HELP_RETRY_STEPS_MAX;
    }
}

help_event_t *help_queue_push(help_queue_t *q, uint32_t seq, uint32_t boot_id, uint64_t now_us)
{
    if (q == NULL) {
        return NULL;
    }

    for (uint16_t i = 0; i < HELP_QUEUE_MAX; i++) {
        if (q->slots[i].used) {
            continue;
        }
        if (q->count >= q->cfg.capacity) {
            break; /* bounded: refuse rather than grow */
        }

        help_event_t *e = &q->slots[i];
        memset(e, 0, sizeof(*e));
        e->used = true;
        e->seq = seq;
        e->boot_id = boot_id;
        e->created_us = now_us;
        e->attempt_us = 0;
        e->next_attempt_us = now_us; /* due immediately */
        e->attempts = 0;
        e->state = HELP_ST_RECORDED;
        q->count++;
        return e;
    }

    q->refused++;
    return NULL;
}

static bool slot_due(const help_event_t *e, uint64_t now_us)
{
    if (!e->used) {
        return false;
    }
    switch (e->state) {
    case HELP_ST_RECORDED:
        return true; /* eligible as soon as it exists */
    case HELP_ST_PENDING:
    case HELP_ST_UNKNOWN:
        return now_us >= e->next_attempt_us;
    default:
        return false;
    }
}

help_event_t *help_queue_due(help_queue_t *q, uint64_t now_us)
{
    if (q == NULL) {
        return NULL;
    }
    help_event_t *best = NULL;
    for (uint16_t i = 0; i < HELP_QUEUE_MAX; i++) {
        help_event_t *e = &q->slots[i];
        if (!slot_due(e, now_us)) {
            continue;
        }
        if ((best == NULL) || (e->seq < best->seq) ||
            ((e->seq == best->seq) && (e->created_us < best->created_us))) {
            best = e;
        }
    }
    return best;
}

void help_queue_begin(help_queue_t *q, help_event_t *e, uint64_t now_us)
{
    (void)q;
    if (e == NULL) {
        return;
    }
    e->state = HELP_ST_SUBMITTING;
    e->attempt_us = now_us;
    if (e->attempts < 0xFF) {
        e->attempts++;
    }
}

void help_queue_result(help_queue_t *q, help_event_t *e, bool ok, bool timed_out, uint64_t now_us)
{
    if ((q == NULL) || (e == NULL)) {
        return;
    }

    /* A finished event ignores further answers: otherwise repeated results
       would keep mutating (and re-counting) it. Delivery reports, which the
       document mentions as a later refinement, are not implemented. */
    if (help_state_terminal(e->state)) {
        return;
    }

    /*
     * Defensive: a result that arrives without a matching help_queue_begin()
     * still counts as an attempt. Otherwise a channel that answers
     * asynchronously could bypass the attempt ceiling and retry forever.
     */
    if ((e->state != HELP_ST_SUBMITTING) && (e->attempts < 0xFF)) {
        e->attempts++;
    }

    if (ok) {
        e->state = HELP_ST_SUBMITTED;
        q->submitted++;
        return;
    }

    if (timed_out) {
        /* The modem may have sent it: report "unknown" and retry, but never
           claim success. */
        e->state = HELP_ST_UNKNOWN;
        q->unknown++;
    }
    else {
        e->state = HELP_ST_PENDING;
    }

    if (e->attempts >= q->cfg.max_attempts) {
        e->state = HELP_ST_FAILED;
        q->failed++;
        return;
    }

    e->next_attempt_us = now_us + MS(help_backoff_ms(&q->cfg, e->attempts));
}

void help_queue_drop(help_queue_t *q, help_event_t *e)
{
    if ((q == NULL) || (e == NULL) || !e->used) {
        return;
    }
    e->used = false;
    if (q->count > 0) {
        q->count--;
    }
}

uint32_t help_backoff_ms(const help_policy_cfg_t *cfg, uint8_t attempt)
{
    if ((cfg == NULL) || (cfg->retry_steps == 0)) {
        return 0;
    }
    uint8_t index = (attempt == 0) ? 0 : (uint8_t)(attempt - 1);
    if (index >= cfg->retry_steps) {
        index = (uint8_t)(cfg->retry_steps - 1); /* hold at the last step */
    }
    return cfg->retry_ms[index];
}

bool help_state_terminal(help_state_t s)
{
    return (s == HELP_ST_SUBMITTED) || (s == HELP_ST_FAILED);
}

bool help_queue_busy(const help_queue_t *q)
{
    if (q == NULL) {
        return false;
    }
    for (uint16_t i = 0; i < HELP_QUEUE_MAX; i++) {
        if (q->slots[i].used && !help_state_terminal(q->slots[i].state)) {
            return true;
        }
    }
    return false;
}

const char *help_state_name(help_state_t s)
{
    switch (s) {
    case HELP_ST_RECORDED:   return "recorded";
    case HELP_ST_PENDING:    return "pending";
    case HELP_ST_SUBMITTING: return "submitting";
    case HELP_ST_SUBMITTED:  return "submitted";
    case HELP_ST_UNKNOWN:    return "unknown";
    case HELP_ST_FAILED:     return "failed";
    default:                 return "?";
    }
}
