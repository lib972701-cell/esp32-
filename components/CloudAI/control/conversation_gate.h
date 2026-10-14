#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 纯状态控制；跨任务访问必须由调用者加锁，不允许旧连接完成后重新开启录音。 */
typedef struct { bool requested; uint32_t generation; bool busy; } conversation_gate_t;
static inline uint32_t conversation_gate_start(conversation_gate_t *g) {
    if (!g->requested) { g->requested = true; g->busy = true; ++g->generation; }
    return g->generation;
}
static inline void conversation_gate_stop(conversation_gate_t *g) {
    g->requested = false;
    ++g->generation;
}
static inline bool conversation_gate_current(const conversation_gate_t *g, uint32_t generation) {
    return g->requested && g->generation == generation;
}
/* 停止意图只撤销代次；音频/通道已关闭后才能确认停止。
 * 若期间已有新启动意图，旧关闭回调不能确认新会话已停止。 */
static inline void conversation_gate_closed(conversation_gate_t *g) {
    if(!g->requested)g->busy=false;
}
static inline bool conversation_gate_stopped(const conversation_gate_t *g) { return !g->busy; }
