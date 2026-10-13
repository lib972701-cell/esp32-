/*
 * 业务服务中心：拥有运行凭据、单项工作队列和状态锁，串行调度 DTU 查询与邮件。
 * 公开请求接口用于普通任务上下文；长耗时查询、TLS 和 SMTP 由 dtu_worker 执行。
 */
/*
 * 业务服务中心：拥有运行凭据、单项工作队列和状态锁，串行调度 DTU 查询与邮件。
 * 公开请求接口用于普通任务上下文；长耗时查询、TLS 和 SMTP 由 dtu_worker 执行。
 */
/* 应用服务：拥有凭据、任务队列和状态，串行调度定位及邮件。 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "app_service.h"
#include "mail_service.h"
#include "location_service.h"
#include "location_query.h"
#include "location_mail.h"
#include "fall_mail.h"
#include "dtu_uart.h"
#include "dtu_time.h"
#include "boot_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
static const char *TAG="mail_service";
static SemaphoreHandle_t state_lock; /* 保护凭据和共享状态，不跨耗时网络操作持锁。 */
static QueueHandle_t jobs;           /* 容量为 16；入队复制任务，唯一 worker 独占 UART。 */
static mail_credentials_t credentials;
static bool wiring_ok,busy,configured,clock_set;
static mail_result_t last_result=MAIL_FAILED;
static bool mail_result_ready;
static bool setup_reserved;
static const char *result_names[]={"FAILED","REJECTED","UNKNOWN_NO_RETRY","DATA_ACCEPTED","PROBE_OK"};
static location_result_t last_location;
static int64_t location_completed_us;
/* probe：仅探测；location：独立定位；attach_location：定位后组正文发信。
 * autonomous：上电初始化及校时；clock_only：只校时；show_location：本地显示坐标。
 * 各标志由内部构造函数组合，控制台不直接提交任意标志组合。 */
typedef struct { bool probe; bool location; bool show_location; bool attach_location; bool autonomous; bool clock_only; bool fall; uint32_t event_id; uint64_t event_us; location_source_t source; mail_message_t message; } mail_job_t;

/*
 * 清除旧校时标志，在 90 秒总预算内读取 DTU 时间并设置系统 UTC 时钟。
 * 查询等待期间不持有状态锁；只在更新 clock_set 时加锁，避免阻塞状态查询。
 */
/*
 * 清除旧校时标志，在 90 秒总预算内读取 DTU 时间并设置系统 UTC 时钟。
 * 查询等待期间不持有状态锁；只在更新 clock_set 时加锁，避免阻塞状态查询。
 */
static bool sync_dtu_clock(void)
{
    xSemaphoreTake(state_lock,portMAX_DELAY);clock_set=false;xSemaphoreGive(state_lock);
    int64_t end=esp_timer_get_time()+90000000;
    do {
        int64_t epoch;
        if(dtu_time_query(boot_time_offset_minutes(),&epoch,end)) {
            struct timeval tv={.tv_sec=(time_t)epoch,.tv_usec=0};
            if(settimeofday(&tv,NULL)==0) {
                xSemaphoreTake(state_lock,portMAX_DELAY);clock_set=true;xSemaphoreGive(state_lock);
                ESP_LOGI(TAG,"DTU_TIME_OK epoch=%lld offset_minutes=%d",(long long)epoch,boot_time_offset_minutes());return true;
            }
        }
        int64_t remaining=(end-esp_timer_get_time())/1000;
        if(remaining<=0)break;
        vTaskDelay(pdMS_TO_TICKS(remaining<2000 ? remaining : 2000));
    } while(esp_timer_get_time()<end);
    ESP_LOGE(TAG,"DTU_TIME_FAILED: no email without valid clock");return false;
}

/*
 * 唯一 DTU 业务工作任务，永久等待队列：自动校时、独立定位、定位邮件或普通邮件。
 * 每条退出路径均释放 busy；任务和临时凭据使用后清零。定位失败可形成说明正文，
 * 校时失败则停止自动邮件；邮件提交后结果未知也不自动重发。
 */
/*
 * 唯一 DTU 业务工作任务，永久等待队列：自动校时、独立定位、定位邮件或普通邮件。
 * 每条退出路径均释放 busy；任务和临时凭据使用后清零。定位失败可形成说明正文，
 * 校时失败则停止自动邮件；邮件提交后结果未知也不自动重发。
 */
/* 只由 DTU worker 调用；有界重试，UNKNOWN 禁止重发。 */
static mail_result_t send_fall(const mail_job_t *job)
{
    mail_result_t result = MAIL_FAILED;
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        bool ready = dtu_uart_init() == ESP_OK;
        xSemaphoreTake(state_lock, portMAX_DELAY);
        wiring_ok = ready;
        bool valid_clock = clock_set;
        bool valid_config = configured;
        mail_credentials_t c = credentials;
        xSemaphoreGive(state_lock);
        if (!valid_config) {
            mail_secure_zero(&c, sizeof(c));
            ESP_LOGE(TAG, "FALL_MAIL_CONFIG_MISSING event=%lu", (unsigned long)job->event_id);
            break;
        }
        if (ready && !valid_clock) ready = sync_dtu_clock();
        mail_message_t message = {0};
        strcpy(message.subject, "【紧急求助】疑似摔倒");
        char base[401];
        ready = ready && fall_mail_body(base, sizeof(base), job->event_id, job->event_us);
        if (ready) {
            location_result_t position = location_query(LOCATION_LBS);
            ready = location_mail_body(message.body, sizeof(message.body), base,
                                       &position, (int64_t)time(NULL));
            mail_secure_zero(&position, sizeof(position));
        }
        result = ready ? mail_link_run(&c, &message, false) : MAIL_FAILED;
        mail_secure_zero(&c, sizeof(c));
        mail_secure_zero(base, sizeof(base));
        mail_secure_zero(&message, sizeof(message));
        ESP_LOGW(TAG, "FALL_MAIL_RESULT event=%lu attempt=%u result=%s",
                 (unsigned long)job->event_id, attempt, result_names[result]);
        if (!fall_mail_retry(result, attempt)) break;
        vTaskDelay(pdMS_TO_TICKS(attempt == 1 ? 5000 : 15000));
    }
    return result;
}

static void worker(void *unused)
{
    (void)unused;
    for (;;) {
        mail_job_t job;
        xQueueReceive(jobs,&job,portMAX_DELAY);
        if (job.fall) {
            mail_result_t r = send_fall(&job);
            mail_secure_zero(&job, sizeof(job));
            xSemaphoreTake(state_lock, portMAX_DELAY);
            last_result = r; mail_result_ready = true;
            busy = uxQueueMessagesWaiting(jobs) != 0;
            xSemaphoreGive(state_lock);
            continue;
        }
        if(job.autonomous || job.clock_only) {
            bool ok=true;
            if(job.autonomous) {
                ESP_LOGI(TAG,"BOOT_DTU_INIT: no test email");
                ok=dtu_uart_init()==ESP_OK;
                xSemaphoreTake(state_lock,portMAX_DELAY);wiring_ok=ok;xSemaphoreGive(state_lock);
                if(ok)vTaskDelay(pdMS_TO_TICKS(20000));
            }
            ok=ok && sync_dtu_clock();
            if(!ok || job.clock_only || job.autonomous) {
                xSemaphoreTake(state_lock,portMAX_DELAY);
                busy=uxQueueMessagesWaiting(jobs)!=0;xSemaphoreGive(state_lock);
                if(job.autonomous)ESP_LOGI(TAG,"BOOT_DTU_READY=%d",ok);
                mail_secure_zero(&job,sizeof(job));continue;
            }
        }
        if (job.location) {
            location_result_t result=location_query(job.source);
            xSemaphoreTake(state_lock,portMAX_DELAY);
            last_location=result;location_completed_us=esp_timer_get_time();busy=uxQueueMessagesWaiting(jobs)!=0;
            xSemaphoreGive(state_lock);
            printf("LOCATION_RESULT source=%s status=%s elapsed_ms=%lu error=%d\n",
                result.source==LOCATION_LBS ? "LBS" : "GNSS",location_status_name(result.status),
                (unsigned long)result.elapsed_ms,result.error_code);
            if (job.show_location && result.status==LOCATION_VALID)
                printf("LOCATION_COORD WGS84 lon=%.7f lat=%.7f\n",result.longitude,result.latitude);
            mail_secure_zero(&job,sizeof(job));
            continue;
        }
        bool composed=true;
        if (job.attach_location) {
            location_result_t position=location_query(job.source);
            char base[401];strcpy(base,job.message.body);
            composed=location_mail_body(job.message.body,sizeof(job.message.body),base,&position,(int64_t)time(NULL));
            mail_secure_zero(base,sizeof(base));
            ESP_LOGI(TAG,"LOCATION_FOR_MAIL source=%s status=%s elapsed_ms=%lu",
                job.source==LOCATION_LBS ? "LBS" : "GNSS",location_status_name(position.status),
                (unsigned long)position.elapsed_ms);
            /* Preserve no coordinates: mail_link_run will reboot the DTU. */
            mail_secure_zero(&position,sizeof(position));
        }
        mail_credentials_t c;
        xSemaphoreTake(state_lock,portMAX_DELAY); c=credentials; xSemaphoreGive(state_lock);
        mail_result_t r=composed ? mail_link_run(&c,&job.message,job.probe) : MAIL_FAILED;
        mail_secure_zero(&c,sizeof(c));
        mail_secure_zero(&job,sizeof(job));
        xSemaphoreTake(state_lock,portMAX_DELAY);
        last_result=r;mail_result_ready=true;busy=uxQueueMessagesWaiting(jobs)!=0;
        xSemaphoreGive(state_lock);
        ESP_LOGI(TAG,"MAIL_RESULT=%s",result_names[r]);
    }
}
/*
 * 统一邮件入队入口：检查字节长度、定位来源以及接线、时钟、配置和 busy 状态。
 * 在锁内预占 busy 后非阻塞入队，队列复制任务内容；入队失败立即撤销预占。
 * 邮件会重启 DTU，因此提交新邮件任务时先使独立定位缓存失效。
 */
/*
 * 统一邮件入队入口：检查字节长度、定位来源以及接线、时钟、配置和 busy 状态。
 * 在锁内预占 busy 后非阻塞入队，队列复制任务内容；入队失败立即撤销预占。
 * 邮件会重启 DTU，因此提交新邮件任务时先使独立定位缓存失效。
 */
static esp_err_t request_job(bool probe, const char *subject, const char *body, bool attach, location_source_t source)
{
    if (attach && source!=LOCATION_LBS && source!=LOCATION_GNSS) return ESP_ERR_INVALID_ARG;
    mail_job_t job={.probe=probe,.attach_location=attach,.source=source};
    if (!probe) {
        if (!subject || !body || !subject[0] || strlen(subject)>45 || strlen(body)>(attach ? 400 : 768)) return ESP_ERR_INVALID_ARG;
        strcpy(job.message.subject,subject); strcpy(job.message.body,body);
    }
    xSemaphoreTake(state_lock,portMAX_DELAY);
    esp_err_t e=ESP_OK;
    if (busy || !wiring_ok || !clock_set || (!probe && !configured)) e=ESP_ERR_INVALID_STATE;
    else {
        busy=true;
        mail_result_ready=false;
        /* Mail currently reboots the DTU; prior query results cannot remain current. */
        location_completed_us=0;memset(&last_location,0,sizeof(last_location));
        if (xQueueSend(jobs,&job,0)!=pdTRUE) { busy=false; e=ESP_ERR_NO_MEM; }
    }
    xSemaphoreGive(state_lock);
    mail_secure_zero(&job,sizeof(job));
    return e;
}
/*
 * 普通邮件与探测请求的内部适配；不附加位置，探测不要求邮件正文。
 */
/*
 * 普通邮件与探测请求的内部适配；不附加位置，探测不要求邮件正文。
 */
static esp_err_t request(bool probe, const char *subject, const char *body)
{ return request_job(probe,subject,body,false,LOCATION_LBS); }
/*
 * 提交一次“新定位查询 + 正文构造 + 单封发送”组合任务，不使用定位缓存。
 */
/*
 * 提交一次“新定位查询 + 正文构造 + 单封发送”组合任务，不使用定位缓存。
 */
esp_err_t mail_request_with_location(const char *subject, const char *base_body, location_source_t source)
{ return request_job(false,subject,base_body,true,source); }
/*
 * 读取最近完成的邮件结果；未完成或尚无结果时返回 ESP_ERR_INVALID_STATE。
 */
/*
 * 读取最近完成的邮件结果；未完成或尚无结果时返回 ESP_ERR_INVALID_STATE。
 */
esp_err_t mail_get_last_result(mail_result_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=MAIL_FAILED;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    bool ready=mail_result_ready;
    if (ready) *out=last_result;
    xSemaphoreGive(state_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}
/*
 * 复制调用方主题和正文到任务队列，接口返回成功仅表示已入队。
 */
/*
 * 复制调用方主题和正文到任务队列，接口返回成功仅表示已入队。
 */
esp_err_t mail_request_text(const char *subject, const char *body) { return request(false,subject,body); }
/*
 * 构造固定的非紧急测试内容，提交一封普通测试邮件。
 */
/*
 * 构造固定的非紧急测试内容，提交一封普通测试邮件。
 */
esp_err_t mail_request_test(void) {
    return mail_request_text("ESP32-S3 4G 邮件通道测试",
        "这是一封由 ESP32-S3 通过 UART 和 4G DTU 直接发送的测试邮件。\r\n"
        "并非真实求助，不包含真实位置或紧急事件。\r\n");
}
/*
 * 提交独立定位工作；开始新查询即使旧缓存失效，show 仅控制本地坐标输出。
 */
/*
 * 提交独立定位工作；开始新查询即使旧缓存失效，show 仅控制本地坐标输出。
 */
esp_err_t app_service_location_request(location_source_t source, bool show)
{
    if (source!=LOCATION_LBS && source!=LOCATION_GNSS) return ESP_ERR_INVALID_ARG;
    mail_job_t job={.location=true,.show_location=show,.source=source};
    xSemaphoreTake(state_lock,portMAX_DELAY);
    esp_err_t e=ESP_OK;
    if (busy || !wiring_ok) e=ESP_ERR_INVALID_STATE;
    else {
        busy=true;
        /* Invalidate the previous position as soon as a new query begins. */
        location_completed_us=0;memset(&last_location,0,sizeof(last_location));
        if (xQueueSend(jobs,&job,0)!=pdTRUE) {busy=false;e=ESP_ERR_NO_MEM;}
    }
    xSemaphoreGive(state_lock);
    return e;
}
/*
 * 提交不打印坐标的独立定位请求，结果后续通过 location_get_last() 获取。
 */
/*
 * 提交不打印坐标的独立定位请求，结果后续通过 location_get_last() 获取。
 */
esp_err_t location_request(location_source_t source) { return app_service_location_request(source,false); }
/*
 * 复制独立定位结果，并返回距查询完成的毫秒数；该年龄不是定位数据年龄。
 * 调用方必须检查 status==LOCATION_VALID 才能使用坐标；暂无缓存时返回状态错误。
 */
/*
 * 复制独立定位结果，并返回距查询完成的毫秒数；该年龄不是定位数据年龄。
 * 调用方必须检查 status==LOCATION_VALID 才能使用坐标；暂无缓存时返回状态错误。
 */
esp_err_t location_get_last(location_result_t *out, uint32_t *query_age_ms)
{
    if (!out || !query_age_ms) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));out->status=LOCATION_NO_FIX;*query_age_ms=0;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (!location_completed_us) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    *out=last_location;
    int64_t age=(esp_timer_get_time()-location_completed_us)/1000;
    *query_age_ms=age>UINT32_MAX ? UINT32_MAX : (uint32_t)age;
    xSemaphoreGive(state_lock);
    return ESP_OK;
}

/*
 * 创建锁、容量为 16 的队列和 worker（16 KiB 栈，优先级 5）。
 * 装载私有配置，仅入队 DTU 初始化与校时，上电不发送邮件。
 */
esp_err_t app_service_start(void) {
    if (state_lock) return ESP_ERR_INVALID_STATE;
    state_lock=xSemaphoreCreateMutex();jobs=xQueueCreate(16,sizeof(mail_job_t));
    if (!state_lock || !jobs) {
        goto allocation_failed;
    }
    {
        mail_credentials_t local={0};
        if(boot_mail_credentials(&local) && app_service_config_begin()==ESP_OK)
            (void)app_service_config_finish(&local);
        mail_secure_zero(&local,sizeof(local));
        mail_job_t job={.autonomous=true};
        busy=true;
        if(xQueueSend(jobs,&job,0)!=pdTRUE){mail_secure_zero(&job,sizeof(job));goto allocation_failed;}
        mail_secure_zero(&job,sizeof(job));
    }
    if(xTaskCreate(worker,"dtu_worker",16384,NULL,5,NULL)!=pdPASS)goto allocation_failed;
    return ESP_OK;
allocation_failed:
    if(jobs)vQueueDelete(jobs);
    if(state_lock)vSemaphoreDelete(state_lock);
    jobs=NULL;state_lock=NULL;busy=false;setup_reserved=false;
    configured=false;clock_set=false;wiring_ok=false;mail_result_ready=false;
    mail_secure_zero(&credentials,sizeof(credentials));return ESP_ERR_NO_MEM;
}

esp_err_t app_service_fall_request(uint32_t event_id, uint64_t event_us)
{
    if (!state_lock || !jobs || !event_id) return ESP_ERR_INVALID_STATE;
    mail_job_t job = {.fall=true, .event_id=event_id, .event_us=event_us};
    /* 不等待配置会话的锁；采样回调必须有界。 */
    if (xSemaphoreTake(state_lock, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (setup_reserved) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    esp_err_t err = xQueueSend(jobs, &job, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        busy=true; mail_result_ready=false;
        location_completed_us=0; memset(&last_location,0,sizeof(last_location));
    }
    xSemaphoreGive(state_lock);
    return err;
}
/*
 * 在状态锁内生成一致的状态快照；out 必须指向可写结构，服务须已启动。
 */
/*
 * 在状态锁内生成一致的状态快照；out 必须指向可写结构，服务须已启动。
 */
void app_service_status(app_service_status_t *out) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    *out=(app_service_status_t){wiring_ok,clock_set,configured,busy,mail_result_ready,last_result};
    xSemaphoreGive(state_lock);
}
/*
 * 空闲时启用业务 UART 并设置接线状态；接线的电气正确性仍由调用方确认。
 */
/*
 * 空闲时启用业务 UART 并设置接线状态；接线的电气正确性仍由调用方确认。
 */
esp_err_t app_service_enable_uart(void) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    esp_err_t e=busy ? ESP_ERR_INVALID_STATE : dtu_uart_init();
    if (e==ESP_OK) wiring_ok=true;
    xSemaphoreGive(state_lock);return e;
}
/*
 * 接受 2025—2100 范围内的 UTC Unix 秒数；busy 时拒绝手动改时。
 */
/*
 * 接受 2025—2100 范围内的 UTC Unix 秒数；busy 时拒绝手动改时。
 */
esp_err_t app_service_set_time(int64_t epoch) {
    if (epoch<1735689600LL || epoch>4102444800LL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (busy) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    struct timeval tv={.tv_sec=(time_t)epoch,.tv_usec=0};
    clock_set=settimeofday(&tv,NULL)==0;
    esp_err_t e=clock_set ? ESP_OK : ESP_FAIL;
    xSemaphoreGive(state_lock);return e;
}
/*
 * 预占配置会话：设置 busy 与 setup_reserved，防止输入凭据时启动业务。
 */
/*
 * 预占配置会话：设置 busy 与 setup_reserved，防止输入凭据时启动业务。
 */
esp_err_t app_service_config_begin(void) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (busy) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    busy=true;setup_reserved=true;xSemaphoreGive(state_lock);return ESP_OK;
}
/*
 * 结束已预占的配置会话；验证地址、发件域和字母数字授权码后复制到 RAM。
 * NULL 或无效输入会清除旧凭据；所有正常结束路径释放配置预占与 busy。
 */
/*
 * 结束已预占的配置会话；验证地址、发件域和字母数字授权码后复制到 RAM。
 * NULL 或无效输入会清除旧凭据；所有正常结束路径释放配置预占与 busy。
 */
esp_err_t app_service_config_finish(const mail_credentials_t *c) {
    bool ok=c && mail_address_valid(c->sender) && mail_address_valid(c->recipient);
    const char *domain=c ? strrchr(c->sender,'@') : NULL;
    ok=ok && domain && (!strcmp(domain,"@qq.com") || !strcmp(domain,"@foxmail.com"));
    ok=ok && c->authorization[0];
    if (ok) for (const char *p=c->authorization;*p;p++)
        if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9'))) ok=false;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (!setup_reserved) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    mail_secure_zero(&credentials,sizeof(credentials));configured=ok;
    if (ok) credentials=*c;
    busy=false;setup_reserved=false;xSemaphoreGive(state_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
/*
 * 仅在空闲时清除 RAM 凭据；不会改写编译配置，重启仍可能重新加载。
 */
/*
 * 仅在空闲时清除 RAM 凭据；不会改写编译配置，重启仍可能重新加载。
 */
esp_err_t app_service_clear(void) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (busy) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    mail_secure_zero(&credentials,sizeof(credentials));configured=false;
    xSemaphoreGive(state_lock);return ESP_OK;
}
/*
 * 提交只验证 TLS 和 SMTP EHLO 的任务，不认证也不发送邮件。
 */
/*
 * 提交只验证 TLS 和 SMTP EHLO 的任务，不认证也不发送邮件。
 */
esp_err_t app_service_probe(void) { return request(true,NULL,NULL); }
/*
 * 提交独立校时任务，要求 UART 已启用且服务空闲；无需邮件凭据。
 */
/*
 * 提交独立校时任务，要求 UART 已启用且服务空闲；无需邮件凭据。
 */
esp_err_t app_service_clock_request(void) {
    mail_job_t job={.clock_only=true};
    xSemaphoreTake(state_lock,portMAX_DELAY);
    esp_err_t e=ESP_OK;
    if(busy || !wiring_ok)e=ESP_ERR_INVALID_STATE;
    else {busy=true;if(xQueueSend(jobs,&job,0)!=pdTRUE){busy=false;e=ESP_ERR_NO_MEM;}}
    xSemaphoreGive(state_lock);return e;
}
