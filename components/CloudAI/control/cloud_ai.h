#pragma once
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 创建独立初始化任务；联网、配网及服务器等待不阻塞 main 或 fall_sampling。 */
esp_err_t cloud_ai_start(void);
/* 只允许前台调度器调用启动；停止确认包含音频关闭和通道关闭。 */
bool cloud_ai_request_conversation(bool start);
bool cloud_ai_conversation_stopped(void);
#ifdef __cplusplus
}
#endif
