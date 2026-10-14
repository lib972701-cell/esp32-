/* 前台唯一调度者。摔倒/邮件服务不依赖此文件，不属于前台互斥范围。 */
#include "activity_service.h"
#include "vision_uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG="activity";
static SemaphoreHandle_t lock;
static TaskHandle_t task;
static activity_cloud_control_fn cloud_control;
static activity_cloud_stopped_fn cloud_stopped;
static activity_status_t status;
static activity_mode_t intent;
static uint32_t revision;
static bool uart_ready;

void activity_register_cloud(activity_cloud_control_fn control,activity_cloud_stopped_fn stopped) {
    cloud_control=control; cloud_stopped=stopped;
}
static bool send_wire(void *ctx,const char *data,size_t size) {
    (void)ctx; return uart_ready && vision_uart_write(data,size);
}
static bool control_cloud(void *ctx,bool start) {
    (void)ctx;
    if(!cloud_control)return !start;
    ESP_LOGI(TAG,"CLOUD_REQUEST=%s; fall_sampling remains active",start ? "START" : "STOP");
    return cloud_control(start);
}
esp_err_t activity_select(activity_mode_t mode) {
    if(!lock)return ESP_ERR_INVALID_STATE;
    if((unsigned)mode>ACTIVITY_PERSON)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock,portMAX_DELAY);
    intent=mode; ++revision; status.result.valid=false;
    xSemaphoreGive(lock);
    xTaskNotifyGive(task); return ESP_OK;
}
esp_err_t activity_key(unsigned key,bool long_press) {
    if(!lock || key>2)return ESP_ERR_INVALID_STATE;
    activity_mode_t mode=(activity_mode_t)(ACTIVITY_CLOUD+key);
    xSemaphoreTake(lock,portMAX_DELAY);
    if(!long_press || intent==mode) {
        intent=long_press ? ACTIVITY_OFF : mode; ++revision; status.result.valid=false;
    }
    xSemaphoreGive(lock);
    ESP_LOGI(TAG,"KEY%u %s",key,long_press ? "LONG" : "SHORT");
    xTaskNotifyGive(task); return ESP_OK;
}
esp_err_t activity_get_status(activity_status_t *out) {
    if(!out)return ESP_ERR_INVALID_ARG;
    if(!lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY); *out=status; xSemaphoreGive(lock); return ESP_OK;
}
static int cmd_status(int argc,char **argv) {
    (void)argc; (void)argv; activity_status_t s;
    if(activity_get_status(&s)!=ESP_OK)return 1;
    printf("ACTIVITY desired=%s phase=%s remote=%s confirmed=%d cloud_requested=%d cloud_stopped=%d uart_configured=%d error=%s bad_packets=%lu\n",
        activity_mode_name(s.desired),vision_phase_name(s.phase),activity_mode_name(s.remote_mode),
        s.remote_confirmed,s.cloud_requested,s.cloud_stopped,s.uart_configured,
        s.last_error[0] ? s.last_error : "none",(unsigned long)s.protocol_errors);
    printf("VISION valid=%d mode=%s state=%s count=%lu left=%lu front=%lu right=%lu age_ms=%llu\n",
        s.result.valid,activity_mode_name(s.result.mode),s.result.valid ? s.result.state : "unknown",
        (unsigned long)(s.result.valid ? s.result.count : 0),
        (unsigned long)(s.result.valid ? s.result.left : 0),
        (unsigned long)(s.result.valid ? s.result.center : 0),
        (unsigned long)(s.result.valid ? s.result.right : 0),
        (unsigned long long)(s.result.valid ? (uint64_t)(esp_timer_get_time()/1000)-s.result.received_ms : 0));
    return 0;
}
static int cmd_select(int argc,char **argv) {
    if(argc!=2) { puts("activity_select idle|cloud|light|person"); return 1; }
    const char *names[]={"idle","cloud","light","person"};
    for(unsigned i=0;i<4;i++)if(!strcmp(argv[1],names[i]))return activity_select((activity_mode_t)i)==ESP_OK ? 0 : 1;
    puts("activity_select idle|cloud|light|person"); return 1;
}
static void service_task(void *arg) {
    (void)arg; vision_session_t session;
    vision_session_init(&session,vision_uart_configured(),send_wire,control_cloud,NULL);
    uint32_t applied=0,logged_frame=0; bool was_valid=false;
    vision_phase_t old_phase=(vision_phase_t)-1;
    activity_mode_t old_desired=(activity_mode_t)-1;
    char old_error[24]={0};
    for(;;) {
        uint64_t now=(uint64_t)(esp_timer_get_time()/1000);
        xSemaphoreTake(lock,portMAX_DELAY);
        uint32_t current=revision; activity_mode_t requested=intent;
        xSemaphoreGive(lock);
        if(current!=applied) { vision_session_request(&session,requested); applied=current; }
        bool stopped=!cloud_stopped || cloud_stopped();
        /* 先执行互斥控制，再消费UART；切换时旧结果立即不可用。 */
        vision_session_tick(&session,now,stopped);
        if(uart_ready) {
            if(!vision_uart_ok()) {
                vision_session_fault(&session,"UART_INTEGRITY"); session.used=0; session.dropping=false;
            } else {
                uint8_t rx[512]; int n=vision_uart_read(rx,sizeof(rx));
                if(n>0)vision_session_feed(&session,rx,(size_t)n,now);
                else if(n<0)vision_session_fault(&session,"UART_READ_FAILED");
            }
        }
        /* HELLO/FAULT能在接收阶段撤销业务，再tick立即停止云端并处理IDLE。 */
        stopped=!cloud_stopped || cloud_stopped();
        vision_session_tick(&session,now,stopped);
        xSemaphoreTake(lock,portMAX_DELAY);
        if(revision==applied) {
            intent=session.desired;
            status=(activity_status_t){.desired=session.desired,.remote_mode=session.remote_mode,
                .phase=session.phase,.uart_configured=session.configured,.cloud_requested=session.cloud_requested,
                .cloud_stopped=stopped,.remote_confirmed=session.ack_valid && !session.pending,
                .result=session.result,.protocol_errors=session.protocol_errors};
            snprintf(status.last_error,sizeof(status.last_error),"%s",session.last_error);
        } else session.result.valid=false; /* 本轮中途又收到新按键，不发布旧结果。 */
        xSemaphoreGive(lock);
        if(session.phase!=old_phase || session.desired!=old_desired || strcmp(session.last_error,old_error)) {
            ESP_LOGI(TAG,"desired=%s phase=%s remote=%s error=%s; fall_sampling independent",
                activity_mode_name(session.desired),vision_phase_name(session.phase),activity_mode_name(session.remote_mode),
                session.last_error[0] ? session.last_error : "none");
            old_phase=session.phase; old_desired=session.desired; strcpy(old_error,session.last_error);
        }
        if(session.result.valid && (!was_valid || logged_frame!=session.result.frame)) {
            ESP_LOGI("vision","mode=%s state=%s count=%lu L/F/R=%lu/%lu/%lu epoch=%lu frame=%lu",
                activity_mode_name(session.result.mode),session.result.state,(unsigned long)session.result.count,
                (unsigned long)session.result.left,(unsigned long)session.result.center,(unsigned long)session.result.right,
                (unsigned long)session.result.epoch,(unsigned long)session.result.frame);
            logged_frame=session.result.frame;
        } else if(was_valid && !session.result.valid)ESP_LOGW("vision","RESULT_INVALID; state=unknown");
        was_valid=session.result.valid;
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10));
    }
}
esp_err_t activity_service_start(void) {
    if(task)return ESP_OK;
    lock=xSemaphoreCreateMutex(); if(!lock)return ESP_ERR_NO_MEM;
    status.cloud_stopped=true;
    if(vision_uart_configured()) {
        esp_err_t e=vision_uart_init(); uart_ready=e==ESP_OK;
        if(!uart_ready)ESP_LOGE(TAG,"UART2 unavailable: %s; cloud requires remote IDLE confirmation",esp_err_to_name(e));
    } else ESP_LOGW(TAG,"Vision UART2 disabled: configure TX/RX after wiring confirmation; KEY0 available");
    if(xTaskCreate(service_task,"activity",6144,NULL,3,&task)!=pdPASS) {
        vSemaphoreDelete(lock); lock=NULL; return ESP_ERR_NO_MEM;
    }
    esp_console_cmd_t query={.command="activity_status",.help="Show foreground/vision state; fall sampling is independent",.func=cmd_status};
    esp_console_cmd_t select={.command="activity_select",.help="Select idle/cloud/light/person through the same interlock as keys",.func=cmd_select};
    esp_err_t e=esp_console_cmd_register(&query);
    if(e==ESP_OK)e=esp_console_cmd_register(&select);
    return e;
}
