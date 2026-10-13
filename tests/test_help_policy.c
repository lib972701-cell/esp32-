/*
 * Host-side unit tests for the help-event queue policy (design document
 * section 12: bounded queue, delivery states, 5/15/30/60 s backoff, no silent
 * drop, no false "sent").
 *
 * These prove the state machine only. Whether a real modem accepts a short
 * message is a stage-E question and is not claimed here.
 */
#include <stdio.h>
#include <string.h>

#include "help_policy.h"

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

#define SEC 1000000ULL
#define MS_US(ms) ((uint64_t)(ms) * 1000ULL)

static help_queue_t q;

static void reset_queue(void)
{
    help_policy_cfg_t cfg;
    help_policy_defaults(&cfg);
    help_queue_init(&q, &cfg);
}

/* Push one event and return it, failing the test when the queue refuses. */
static help_event_t *push(uint32_t seq, uint64_t now_us)
{
    help_event_t *e = help_queue_push(&q, seq, /*boot_id=*/7, now_us);
    CHECK(e != NULL, "push(seq=%u) was refused with an empty queue", seq);
    return e;
}

static void test_defaults(void)
{
    help_policy_cfg_t cfg;
    help_policy_defaults(&cfg);
    CHECK(cfg.retry_steps == 4, "retry_steps=%u", (unsigned)cfg.retry_steps);
    CHECK(cfg.retry_ms[0] == 5000 && cfg.retry_ms[1] == 15000 &&
          cfg.retry_ms[2] == 30000 && cfg.retry_ms[3] == 60000,
          "retry ladder %u/%u/%u/%u", (unsigned)cfg.retry_ms[0], (unsigned)cfg.retry_ms[1],
          (unsigned)cfg.retry_ms[2], (unsigned)cfg.retry_ms[3]);
    CHECK(cfg.max_attempts == 5, "max_attempts=%u", (unsigned)cfg.max_attempts);
    CHECK(cfg.capacity == HELP_QUEUE_MAX, "capacity=%u", (unsigned)cfg.capacity);

    /* A capacity of 0 must not produce a queue that can never hold anything. */
    cfg.capacity = 0;
    help_queue_init(&q, &cfg);
    CHECK(q.cfg.capacity == 1, "capacity 0 clamped to %u", (unsigned)q.cfg.capacity);

    help_policy_cfg_t big;
    help_policy_defaults(&big);
    big.capacity = 99;
    big.retry_steps = 9;
    help_queue_init(&q, &big);
    CHECK(q.cfg.capacity == HELP_QUEUE_MAX, "capacity 99 clamped to %u", (unsigned)q.cfg.capacity);
    CHECK(q.cfg.retry_steps == HELP_RETRY_STEPS_MAX, "retry_steps 9 clamped to %u",
          (unsigned)q.cfg.retry_steps);
}

static void test_fifo_order(void)
{
    reset_queue();
    const uint64_t t0 = 100 * SEC;
    push(11, t0);
    push(12, t0 + SEC);
    push(13, t0 + 2 * SEC);

    help_event_t *e = help_queue_due(&q, t0);
    CHECK(e != NULL && e->seq == 11, "first due event is seq=%u", e ? e->seq : 0);
    help_queue_begin(&q, e, t0);
    CHECK(e->attempts == 1, "attempts=%u after begin", (unsigned)e->attempts);
    CHECK(e->state == HELP_ST_SUBMITTING, "state=%s after begin", help_state_name(e->state));

    /* The in-flight event itself is not handed over twice (the two later
       events are legitimately due). */
    help_event_t *again = help_queue_due(&q, t0);
    CHECK(again != e, "the SUBMITTING event was offered again");
    CHECK(again != NULL && again->seq == 12, "next due event is seq=%u", again ? again->seq : 0);

    help_queue_result(&q, e, true, false, t0);
    CHECK(e->state == HELP_ST_SUBMITTED, "state=%s after ok", help_state_name(e->state));
    help_queue_drop(&q, e);
    CHECK(q.count == 2, "count=%u after dropping one", (unsigned)q.count);

    e = help_queue_due(&q, t0 + 2 * SEC);
    CHECK(e != NULL && e->seq == 12, "next due event is seq=%u", e ? e->seq : 0);
}

static void test_retry_ladder_and_clamp(void)
{
    reset_queue();
    const uint64_t t0 = 0;
    help_event_t *e = push(1, t0);

    static const uint32_t want[5] = { 5000, 15000, 30000, 60000, 60000 };
    uint64_t now = t0;
    for (unsigned attempt = 1; attempt <= 4; attempt++) {
        e = help_queue_due(&q, now);
        CHECK(e != NULL, "attempt %u: no due event", attempt);
        if (e == NULL) {
            return;
        }
        help_queue_begin(&q, e, now);
        help_queue_result(&q, e, false, false, now);

        CHECK(e->state == HELP_ST_PENDING, "attempt %u: state=%s", attempt,
              help_state_name(e->state));
        CHECK(e->attempts == attempt, "attempt %u: attempts=%u", attempt, (unsigned)e->attempts);

        const uint32_t gap = (uint32_t)((e->next_attempt_us - now) / 1000ULL);
        CHECK(gap == want[attempt - 1], "attempt %u: backoff %u ms, expected %u ms",
              attempt, (unsigned)gap, (unsigned)want[attempt - 1]);

        /* Not due before the backoff has elapsed. */
        CHECK(help_queue_due(&q, e->next_attempt_us - 1) == NULL,
              "attempt %u: retried before the backoff elapsed", attempt);

        now = e->next_attempt_us;
    }

    /* attempt 4 was the last allowed (max_attempts = 5 means five tries), so
       the fifth failure ends in FAILED instead of another pending slot. */
    e = help_queue_due(&q, now);
    CHECK(e != NULL, "fifth attempt not offered");
    if (e != NULL) {
        help_queue_begin(&q, e, now);
        CHECK(e->attempts == 5, "attempts=%u before the fifth result", (unsigned)e->attempts);
        help_queue_result(&q, e, false, false, now);
        CHECK(e->state == HELP_ST_FAILED, "after max attempts state=%s", help_state_name(e->state));
        CHECK(q.failed == 1, "failed counter=%u", (unsigned)q.failed);
    }
}

static void test_timeout_is_unknown_and_retried(void)
{
    reset_queue();
    const uint64_t t0 = 5 * SEC;
    help_event_t *e = push(3, t0);
    help_queue_begin(&q, e, t0);
    help_queue_result(&q, e, false, /*timed_out=*/true, t0);

    CHECK(e->state == HELP_ST_UNKNOWN, "timeout state=%s (must not be 'failed')",
          help_state_name(e->state));
    CHECK(q.unknown == 1, "unknown counter=%u", (unsigned)q.unknown);
    CHECK(q.failed == 0, "a timeout must not count as failed");
    CHECK(e->next_attempt_us == t0 + MS_US(5000), "timeout retry scheduled at +%llu us",
          (unsigned long long)(e->next_attempt_us - t0));
    CHECK(help_queue_busy(&q), "an UNKNOWN event keeps the queue busy");

    /* And it can still succeed later. */
    help_event_t *again = help_queue_due(&q, e->next_attempt_us);
    CHECK(again == e, "the UNKNOWN event is due again after the backoff");
    help_queue_begin(&q, e, e->next_attempt_us);
    help_queue_result(&q, e, true, false, e->next_attempt_us);
    CHECK(e->state == HELP_ST_SUBMITTED, "recovered state=%s", help_state_name(e->state));
}

static void test_bounded_queue_refuses_loudly(void)
{
    help_policy_cfg_t cfg;
    help_policy_defaults(&cfg);
    cfg.capacity = 3;
    help_queue_init(&q, &cfg);

    for (uint32_t i = 0; i < 3; i++) {
        CHECK(help_queue_push(&q, i + 1, 1, 0) != NULL, "push %u refused early", i + 1);
    }
    CHECK(q.count == 3, "count=%u at capacity", (unsigned)q.count);

    help_event_t *full = help_queue_push(&q, 4, 1, 0);
    CHECK(full == NULL, "the fourth event was accepted despite capacity 3");
    CHECK(q.refused == 1, "refused counter=%u", (unsigned)q.refused);
    CHECK(q.count == 3, "count changed to %u after a refusal", (unsigned)q.count);

    /* Dropping a finished event frees the slot again. */
    help_event_t *e = help_queue_due(&q, 0);
    help_queue_begin(&q, e, 0);
    help_queue_result(&q, e, true, false, 0);
    help_queue_drop(&q, e);
    CHECK(q.count == 2, "count=%u after drop", (unsigned)q.count);
    CHECK(help_queue_push(&q, 5, 1, 0) != NULL, "the freed slot was not reused");
    CHECK(q.refused == 1, "refused counter changed on a successful push");
}

static void test_busy_and_terminal(void)
{
    reset_queue();
    CHECK(!help_queue_busy(&q), "an empty queue must not report busy");

    help_event_t *e = push(1, 0);
    CHECK(help_queue_busy(&q), "a RECORDED event must report busy");

    help_queue_begin(&q, e, 0);
    help_queue_result(&q, e, true, false, 0);
    CHECK(!help_queue_busy(&q), "a SUBMITTED event must not report busy");

    help_event_t *f = push(2, 0);
    help_queue_drop(&q, e);
    for (int i = 0; i < 10; i++) {
        help_queue_begin(&q, f, (uint64_t)i * 120 * SEC);
        help_queue_result(&q, f, false, false, (uint64_t)i * 120 * SEC);
    }
    CHECK(f->state == HELP_ST_FAILED, "state=%s after many failures", help_state_name(f->state));
    CHECK(!help_queue_busy(&q), "a FAILED event must not report busy");
    CHECK(help_state_terminal(HELP_ST_SUBMITTED) && help_state_terminal(HELP_ST_FAILED),
          "terminal states not recognised");
    CHECK(!help_state_terminal(HELP_ST_PENDING) && !help_state_terminal(HELP_ST_UNKNOWN) &&
          !help_state_terminal(HELP_ST_RECORDED) && !help_state_terminal(HELP_ST_SUBMITTING),
          "a non-terminal state was reported terminal");
}

static void test_result_without_begin(void)
{
    /* A channel that answers before begin() must not bypass the attempt
       ceiling: the answer is counted as one attempt. */
    reset_queue();
    help_event_t *e = push(9, 0);
    help_queue_result(&q, e, true, false, 0);
    CHECK(e->state == HELP_ST_SUBMITTED, "state=%s", help_state_name(e->state));
    CHECK(e->attempts == 1, "attempts=%u after an unmatched result", (unsigned)e->attempts);

    /* A terminal event ignores later answers (e.g. a very late modem reply). */
    help_queue_result(&q, e, false, false, 100 * SEC);
    CHECK(e->state == HELP_ST_SUBMITTED && e->attempts == 1,
          "a terminal event was mutated: state=%s attempts=%u",
          help_state_name(e->state), (unsigned)e->attempts);

    /* ... and an unmatched *failure* cannot retry forever either. */
    help_event_t *f = push(10, 0);
    for (int i = 0; i < 10; i++) {
        help_queue_result(&q, f, false, false, (uint64_t)(i + 1) * 120 * SEC);
    }
    CHECK(f->attempts == 5, "unmatched failures stopped at attempts=%u", (unsigned)f->attempts);
    CHECK(f->state == HELP_ST_FAILED,
          "unmatched failures bypassed the ceiling: state=%s", help_state_name(f->state));

    /* NULL safety: none of these may crash. */
    help_queue_due(NULL, 0);
    help_queue_begin(&q, NULL, 0);
    help_queue_result(&q, NULL, true, false, 0);
    help_queue_drop(&q, NULL);
    help_queue_drop(&q, e);
    CHECK(!help_queue_busy(NULL), "help_queue_busy(NULL) must be false");
    CHECK(help_backoff_ms(NULL, 1) == 0, "help_backoff_ms(NULL) must be 0");
    CHECK(help_state_name((help_state_t)99) != NULL, "unknown state name must not be NULL");
}

int main(void)
{
    printf("== help-event queue policy (design document section 12)\n");
    test_defaults();
    printf("== FIFO order and single hand-over\n");
    test_fifo_order();
    printf("== retry ladder 5/15/30/60 s and the attempt ceiling\n");
    test_retry_ladder_and_clamp();
    printf("== a submit timeout is 'unknown', not 'failed'\n");
    test_timeout_is_unknown_and_retried();
    printf("== bounded queue refuses and counts instead of dropping silently\n");
    test_bounded_queue_refuses_loudly();
    printf("== busy / terminal semantics\n");
    test_busy_and_terminal();
    printf("== defensive paths\n");
    test_result_without_begin();

    printf("\n----------------------------------------\n");
    printf("checks passed: %d, failed: %d\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
