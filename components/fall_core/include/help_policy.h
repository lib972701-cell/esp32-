/*
 * Help-event queue policy: bounded FIFO, delivery state machine, retry backoff.
 *
 * Implements the queueing half of section 12 of the design document:
 *   - a bounded queue of help events (the document suggests at most 16);
 *   - the delivery states it names: recorded, pending, submitting, submitted,
 *     unknown (submit timed out - it may have been sent), failed;
 *   - retries with a 5 / 15 / 30 / 60 s backoff and a maximum attempt count;
 *   - a full queue refuses the event and counts it, so it is never a silent
 *     drop (the document requires a warning and a record instead).
 *
 * Pure C, no allocation, no ESP-IDF dependency: this is the part that can be
 * proven on the host (host/test_help_policy.c) while the persistence and the
 * channel live in main/.
 *
 * What this layer deliberately does NOT do: talk to a modem, decide where the
 * event is stored, or format the event id. Those are platform concerns and stay
 * in components/help_service/help_store.c and help_channel.c.
 */
#ifndef HELP_POLICY_H
#define HELP_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capacity ceiling of the in-RAM queue; the configured capacity may be lower. */
#define HELP_QUEUE_MAX 16

/* Number of backoff steps the configuration can hold. */
#define HELP_RETRY_STEPS_MAX 4

/* Delivery state of one help event (design document section 12). */
typedef enum {
    HELP_ST_RECORDED = 0, /* written to storage, not yet handed over   */
    HELP_ST_PENDING,      /* failed once, waiting for its next attempt */
    HELP_ST_SUBMITTING,   /* handed to the channel, result not in yet  */
    HELP_ST_SUBMITTED,    /* the channel accepted it                   */
    HELP_ST_UNKNOWN,      /* submit timed out: it may have been sent   */
    HELP_ST_FAILED        /* attempts exhausted                        */
} help_state_t;

typedef struct {
    uint32_t     seq;             /* the detector's event_seq                 */
    uint32_t     boot_id;         /* which boot produced the event            */
    uint64_t     created_us;      /* when the cancel window expired           */
    uint64_t     attempt_us;      /* when the current attempt was handed over */
    uint64_t     next_attempt_us; /* earliest time for the next attempt       */
    uint8_t      attempts;        /* submissions already tried                */
    help_state_t state;
    bool         used;
} help_event_t;

typedef struct {
    uint32_t retry_ms[HELP_RETRY_STEPS_MAX];
    uint8_t  retry_steps;
    uint8_t  max_attempts;
    uint16_t capacity; /* clamped to HELP_QUEUE_MAX */
} help_policy_cfg_t;

typedef struct {
    help_policy_cfg_t cfg;
    help_event_t      slots[HELP_QUEUE_MAX];
    uint16_t          count;     /* events currently held            */
    uint32_t          refused;   /* refused because the queue was full */
    uint32_t          submitted; /* accepted by the channel          */
    uint32_t          failed;    /* attempts exhausted               */
    uint32_t          unknown;   /* submit timeouts observed         */
} help_queue_t;

/* 5 / 15 / 30 / 60 s, at most 5 attempts, capacity 16 - the document's
   suggested values. */
void help_policy_defaults(help_policy_cfg_t *cfg);

void help_queue_init(help_queue_t *q, const help_policy_cfg_t *cfg);

/*
 * Record a new event. It starts in HELP_ST_RECORDED and is due immediately.
 * Returns NULL when the queue is full, in which case q->refused has been
 * incremented - the caller must warn, never drop silently.
 */
help_event_t *help_queue_push(help_queue_t *q, uint32_t seq, uint32_t boot_id, uint64_t now_us);

/*
 * The oldest event that may be handed over now (HELP_ST_RECORDED, or
 * HELP_ST_PENDING/HELP_ST_UNKNOWN whose backoff has elapsed), or NULL.
 * Scans in ascending seq order, so delivery stays first-in-first-out.
 */
help_event_t *help_queue_due(help_queue_t *q, uint64_t now_us);

/* Mark the event as handed over: state SUBMITTING, one more attempt counted. */
void help_queue_begin(help_queue_t *q, help_event_t *e, uint64_t now_us);

/*
 * Record the channel's answer.
 *   ok        -> HELP_ST_SUBMITTED (accepted; delivery reporting is the
 *                channel's own business, see section 12)
 *   timed_out -> HELP_ST_UNKNOWN and still retried: a submit timeout may
 *                already have been sent, so it must not be reported as failed
 *   otherwise -> retry after the backoff for this attempt, or HELP_ST_FAILED
 *                once max_attempts is reached
 *
 * An event that is already terminal (SUBMITTED / FAILED) ignores further
 * results, so a late answer cannot keep mutating a finished event. Network
 * delivery reports are not implemented.
 */
void help_queue_result(help_queue_t *q, help_event_t *e, bool ok, bool timed_out, uint64_t now_us);

/* Release the slot (the caller keeps the record in its own storage). */
void help_queue_drop(help_queue_t *q, help_event_t *e);

/* Backoff after `attempt` failed submissions (1-based), clamped to the last
   configured step. Returns 0 when the configuration has no steps. */
uint32_t help_backoff_ms(const help_policy_cfg_t *cfg, uint8_t attempt);

/* True while any event still needs work (not SUBMITTED / FAILED). */
bool help_queue_busy(const help_queue_t *q);

/* True when the event is in a terminal state. */
bool help_state_terminal(help_state_t s);

const char *help_state_name(help_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* HELP_POLICY_H */
