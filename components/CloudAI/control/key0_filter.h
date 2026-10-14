#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { KEY0_NONE, KEY0_SHORT, KEY0_LONG } key0_event_t;
typedef struct {
    uint32_t debounce_ms, long_ms;
    uint64_t candidate_since, pressed_since;
    bool candidate, stable, initialized, armed, long_sent;
} key0_filter_t;

/* 调用者每10ms提供一次读数；读失败后只允许在确认释放后重新武装。 */
static inline void key0_filter_init(key0_filter_t *s, uint32_t debounce, uint32_t hold) {
    *s = (key0_filter_t){ .debounce_ms = debounce, .long_ms = hold };
}
static inline key0_event_t key0_filter_update(key0_filter_t *s, bool valid,
                                             bool pressed, uint64_t now) {
    if (!valid) { s->initialized = false; s->armed = false; return KEY0_NONE; }
    if (!s->initialized) {
        s->initialized = true;
        s->candidate = s->stable = pressed;
        s->candidate_since = now;
        s->armed = false;
        s->long_sent = false;
        return KEY0_NONE;
    }
    if (s->candidate != pressed) {
        s->candidate = pressed;
        s->candidate_since = now;
    }
    if (now - s->candidate_since < s->debounce_ms) return KEY0_NONE;
    if (!s->armed) {
        if (!pressed) { s->armed = true; s->stable = false; }
        return KEY0_NONE;
    }
    if (s->stable != pressed) {
        s->stable = pressed;
        if (pressed) { s->pressed_since = now; s->long_sent = false; }
        else if (!s->long_sent) return KEY0_SHORT;
    }
    if (s->stable && !s->long_sent && now - s->pressed_since >= s->long_ms) {
        s->long_sent = true;
        return KEY0_LONG;
    }
    return KEY0_NONE;
}
