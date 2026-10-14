#pragma once
#include "esp_err.h"
#include "vision_session.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef bool (*activity_cloud_control_fn)(bool start);
typedef bool (*activity_cloud_stopped_fn)(void);
typedef struct {
    activity_mode_t desired, remote_mode;
    vision_phase_t phase;
    bool uart_configured, cloud_requested, cloud_stopped, remote_confirmed;
    vision_result_t result;
    char last_error[24];
    uint32_t protocol_errors;
} activity_status_t;
/* main启动前注册；App不依赖CloudAI，防止组件依赖成环。 */
void activity_register_cloud(activity_cloud_control_fn,activity_cloud_stopped_fn);
esp_err_t activity_service_start(void);
esp_err_t activity_select(activity_mode_t mode);
esp_err_t activity_key(unsigned key,bool long_press);
esp_err_t activity_get_status(activity_status_t *out);
#ifdef __cplusplus
}
#endif
