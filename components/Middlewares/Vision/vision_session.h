#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ACTIVITY_OFF, ACTIVITY_CLOUD, ACTIVITY_LIGHT, ACTIVITY_PERSON } activity_mode_t;
typedef enum { VISION_IDLE, VISION_SYNCING, VISION_SWITCHING, VISION_WAIT_CLOUD_STOP,
               VISION_ACTIVE, VISION_CLOUD_REQUESTED, VISION_UNCONFIGURED } vision_phase_t;
typedef struct {
    bool valid;
    activity_mode_t mode;
    char state[12];
    uint32_t epoch, frame, count, left, center, right;
    uint64_t received_ms;
} vision_result_t;
typedef bool (*vision_send_fn)(void *,const char *,size_t);
typedef bool (*vision_cloud_fn)(void *,bool);

/* 单一任务拥有此状态；跨任务请求/快照由App层同步。 */
typedef struct {
    activity_mode_t desired, remote_mode, pending_mode;
    vision_phase_t phase;
    vision_result_t result;
    bool configured, synchronized, pending, ack_valid, frame_valid;
    bool cloud_requested, cloud_stop_sent, sync_sent;
    uint32_t sequence, pending_seq, epoch, last_frame, attempts, protocol_errors;
    uint64_t sent_ms, sync_ms, last_result_ms, last_byte_ms;
    char line[160], last_error[24];
    size_t used;
    bool dropping, byte_seen;
    vision_send_fn send;
    vision_cloud_fn cloud;
    void *context;
} vision_session_t;

const char *activity_mode_name(activity_mode_t mode);
const char *vision_phase_name(vision_phase_t phase);
uint16_t vision_crc16(const uint8_t *data,size_t n);
size_t vision_encode(const char *body,char *wire,size_t capacity);
void vision_session_init(vision_session_t *,bool,vision_send_fn,vision_cloud_fn,void *);
void vision_session_request(vision_session_t *,activity_mode_t);
void vision_session_key(vision_session_t *,unsigned key,bool long_press);
void vision_session_feed(vision_session_t *,const uint8_t *,size_t,uint64_t now_ms);
void vision_session_tick(vision_session_t *,uint64_t now_ms,bool cloud_stopped);
void vision_session_fault(vision_session_t *,const char *reason);
#ifdef __cplusplus
}
#endif
