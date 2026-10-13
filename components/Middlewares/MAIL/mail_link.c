/*
 * 端到端邮件传输：在 ESP32 执行 mbedTLS、MIME 和 SMTP，DTU 仅透传 TCP 字节。
 * 调用由 Service 串行调度；每次请求建立新连接，不在失败后自动重新投递。
 */
/* 邮件传输：TLS、MIME及SMTP；不拥有业务队列或UART硬件。 */
#include "mail_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/base64.h"
#include "sdkconfig.h"

#include "dtu_uart.h"
#include "location_query.h"
static const char *TAG="mail_link";
typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    int64_t deadline;
    int64_t session_end;
    bool fatal;
} tls_uart_t;

/*
 * 通过 volatile 逐字节清零，避免普通优化删除对敏感临时数据的擦除。
 */
void mail_secure_zero(void *p, size_t n)
{
    volatile unsigned char *q=p;
    while (n--) *q++=0;
}

/* Compatibility entry points; hardware and positioning implementations live separately. */
/*
 * 兼容旧接口，实际初始化委托 BSP；新业务应通过 Service 管理 UART。
 */
esp_err_t mail_link_init(void) { return dtu_uart_init(); }
/*
 * 兼容旧定位入口；同样要求共享 worker 独占 UART。
 */
location_result_t mail_link_location(location_source_t source) { return location_query(source); }
/*
 * 在 90 秒预算内查询通道 1 的 TCP 连接状态，未就绪前不发送 TLS 字节。
 */
static bool wait_tcp_ready(void)
{
    /* A configured channel is not evidence that its TCP socket is connected. */
    int64_t deadline=esp_timer_get_time()+90000000;
    const char *command="config,get,netstatus,1\r\n";
    while(esp_timer_get_time()<deadline) {
        dtu_uart_flush();
        if(!dtu_uart_write((const unsigned char *)command,strlen(command)))return false;
        char line[128];int64_t end=esp_timer_get_time()+3000000;
        if(end>deadline)end=deadline;
        while(dtu_uart_read_line(line,sizeof(line),end)) {
            if(!strcmp(line,"config,netstatus,ok,1")) {
                ESP_LOGI(TAG,"DTU_TCP_READY");return true;
            }
            if(!strncmp(line,"config,netstatus,error",strlen("config,netstatus,error")))return false;
            if(!strcmp(line,"config,netstatus,ok,0"))break;
        }
        int64_t remaining=(deadline-esp_timer_get_time())/1000;
        if(remaining<=0)break;
        vTaskDelay(pdMS_TO_TICKS(remaining<2000 ? remaining : 2000));
    }
    ESP_LOGE(TAG,"DTU_TCP_NOT_READY: no TLS bytes sent");return false;
}

/*
 * 重启 DTU 丢弃旧 SMTP/TCP 会话，等待 20 秒后验证预配置通道和实际连接。
 * 本函数只检查配置，不会把任意模块自动配置成 smtp.qq.com:465 通道。
 */
static bool prepare_dtu(void)
{
    ESP_LOGI(TAG,"DTU_RESTART: new TCP connection, wait 20 seconds");
    dtu_uart_flush();
    const char *cmd="config,set,reboot\r\n";
    if (!dtu_uart_write((const unsigned char *)cmd,strlen(cmd))) return false;
    // Reboot discards the previous server TCP session, including failed DATA.
    vTaskDelay(pdMS_TO_TICKS(20000));
    dtu_uart_flush();
    cmd="config,get,netchaninfo,1\r\n";
    if (!dtu_uart_write((const unsigned char *)cmd,strlen(cmd))) return false;
    char line[768]; int64_t end=esp_timer_get_time()+5000000;
    while (dtu_uart_read_line(line,sizeof(line),end)) {
        if (!strncmp(line,"config,netchaninfo,ok,",strlen("config,netchaninfo,ok,"))) {
            bool ok=mail_dtu_channel_valid(line);
            ESP_LOGI(TAG,"DTU_CONFIG_%s",ok ? "OK" : "MISMATCH");
            return ok && wait_tcp_ready();
        }
        if (!strncmp(line,"config,netchaninfo,error",23)) return false;
    }
    ESP_LOGE(TAG,"DTU query timeout; verify wiring and 115200");
    return false;
}

/*
 * mbedTLS 底层发送回调：将密文分块交给 UART，允许短写并遵守截止时间。
 */
static int bio_send(void *ctx, const unsigned char *buf, size_t n)
{
    tls_uart_t *t=ctx;
    if (t->fatal || esp_timer_get_time()>t->deadline) return MBEDTLS_ERR_SSL_TIMEOUT;
    // A bounded chunk also permits UART integrity checks between TLS fragments.
    if (n>512) n=512;
    if (!dtu_uart_write(buf,n)) { t->fatal=true; return MBEDTLS_ERR_SSL_INTERNAL_ERROR; }
    return (int)n;
}
/*
 * mbedTLS 底层接收回调：直接读取二进制；无数据返回 WANT_READ，便于 TLS 继续等待。
 */
static int bio_recv(void *ctx, unsigned char *buf, size_t n)
{
    tls_uart_t *t=ctx;
    if (t->fatal || !dtu_uart_ok()) { t->fatal=true; return MBEDTLS_ERR_SSL_INTERNAL_ERROR; }
    if (esp_timer_get_time()>t->deadline) return MBEDTLS_ERR_SSL_TIMEOUT;
    int r=dtu_uart_read(buf,n,100);
    if (r<0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return r==0 ? MBEDTLS_ERR_SSL_WANT_READ : r;
}
/*
 * 为 SMTP 提供 TLS 解密读取；短暂让出 CPU，直到成功、失败或截止时间。
 */
static int ssl_read(void *ctx, unsigned char *buf, size_t n)
{
    tls_uart_t *t=ctx;
    int r;
    do {
        r=mbedtls_ssl_read(&t->ssl,buf,n);
        if (r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE)
            vTaskDelay(1);
    } while ((r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE) &&
             esp_timer_get_time()<t->deadline);
    return r;
}
/*
 * 为 SMTP 提供 TLS 加密写入；单次最多 1024 字节，余量由上层 write_all 处理。
 */
static int ssl_write(void *ctx, const unsigned char *buf, size_t n)
{
    tls_uart_t *t=ctx;
    int r;
    if (n>1024) n=1024;
    do {
        r=mbedtls_ssl_write(&t->ssl,buf,n);
        if (r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE)
            vTaskDelay(1);
    } while ((r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE) &&
             esp_timer_get_time()<t->deadline);
    return r;
}
/*
 * Base64 编码并补 NUL；仅用于文本编码，不承担加密或认证保护。
 */
static bool b64(const char *in, char *out, size_t capacity)
{
    size_t len=0;
    if (mbedtls_base64_encode((unsigned char *)out,capacity-1,&len,
                             (const unsigned char *)in,strlen(in))) return false;
    out[len]=0; return true;
}
/*
 * 更新当前 SMTP 阶段截止时间：最多 30 秒，并受整个会话截止时间约束。
 */
static void smtp_stage(void *ctx, const char *name)
{
    tls_uart_t *t=ctx;
    int64_t end=esp_timer_get_time()+30000000;
    t->deadline=end<t->session_end ? end : t->session_end;
    ESP_LOGI(TAG,"SMTP_STAGE=%s",name);
}
/*
 * 构造 UTF-8 文本 MIME：主题和正文 Base64 编码，正文按 76 字符折行。
 * Date 使用系统 UTC，Message-ID 使用当前秒数与随机值；不足容量时返回 false。
 */
static bool make_mime(const mail_credentials_t *c, const mail_message_t *message, char *out, size_t size)
{
    char subject[128],body[1100],wrapped[1200],date[64];
    if (!b64(message->subject,subject,sizeof(subject)) ||
        !b64(message->body,body,sizeof(body))) return false;
    size_t used=0,len=strlen(body);
    for (size_t i=0;i<len;i+=76) {
        size_t chunk=len-i<76 ? len-i : 76;
        memcpy(wrapped+used,body+i,chunk); used+=chunk;
        memcpy(wrapped+used,"\r\n",2); used+=2;
    }
    wrapped[used]=0;
    time_t now=time(NULL); struct tm utc;
    gmtime_r(&now,&utc);
    if (!strftime(date,sizeof(date),"%a, %d %b %Y %H:%M:%S +0000",&utc)) return false;
    int n=snprintf(out,size,
        "From: <%s>\r\nTo: <%s>\r\n"
        "Subject: =?UTF-8?B?%s?=\r\nDate: %s\r\n"
        "Message-ID: <%lld.%08lx@esp32-mail.local>\r\n"
        "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\n%s",
        c->sender,c->recipient,subject,date,(long long)now,(unsigned long)esp_random(),wrapped);
    return n>0 && (size_t)n<size;
}

/*
 * 同步执行单次探测或邮件传输，调用方必须保证 UART1 独占和系统时钟有效。
 * 先重启 DTU，再强制校验证书链、主机名及有效期；TLS 最低 1.2。
 * probe_only 只进行 greeting/EHLO；真实邮件必须取得 DATA 最终 250 才算接受。
 * 清理 TLS 和临时敏感内容后返回结果；未知提交结果不自动重试。
 */
mail_result_t mail_link_run(const mail_credentials_t *c, const mail_message_t *message, bool probe_only)
{
    if (!dtu_uart_ready() || time(NULL)<1735689600) return MAIL_FAILED;
    if (!probe_only && (!mail_address_valid(c->sender) || !mail_address_valid(c->recipient))) return MAIL_FAILED;
    if (!prepare_dtu()) return MAIL_FAILED;
    tls_uart_t *t=calloc(1,sizeof(*t));
    if (!t) return MAIL_FAILED;
    mail_result_t result=MAIL_FAILED;
    mbedtls_ssl_init(&t->ssl); mbedtls_ssl_config_init(&t->config);
    mbedtls_entropy_init(&t->entropy); mbedtls_ctr_drbg_init(&t->rng);
    // This isolated mail prototype uses no ADC/RF peripherals. Revisit ownership
    // before integrating audio/ADC/Wi-Fi; never enable this source concurrently.
    bootloader_random_enable();
    int rc=mbedtls_ctr_drbg_seed(&t->rng,mbedtls_entropy_func,&t->entropy,
                                (const unsigned char *)"serial-4g-mail",14);
    if (rc) goto cleanup;
    rc=mbedtls_ssl_config_defaults(&t->config,MBEDTLS_SSL_IS_CLIENT,
                                   MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) goto cleanup;
    mbedtls_ssl_conf_rng(&t->config,mbedtls_ctr_drbg_random,&t->rng);
    mbedtls_ssl_conf_authmode(&t->config,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_tls_version(&t->config,MBEDTLS_SSL_VERSION_TLS1_2);
    if (esp_crt_bundle_attach(&t->config)!=ESP_OK) goto cleanup;
    rc=mbedtls_ssl_setup(&t->ssl,&t->config); if (rc) goto cleanup;
    rc=mbedtls_ssl_set_hostname(&t->ssl,"smtp.qq.com"); if (rc) goto cleanup;
    mbedtls_ssl_set_bio(&t->ssl,t,bio_send,bio_recv,NULL);
    t->deadline=esp_timer_get_time()+60000000;
    ESP_LOGI(TAG,"TLS_HANDSHAKE");
    do {
        rc=mbedtls_ssl_handshake(&t->ssl);
        if (rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE) vTaskDelay(1);
    } while ((rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE) &&
             esp_timer_get_time()<t->deadline);
    if (rc || mbedtls_ssl_get_verify_result(&t->ssl)) {
        ESP_LOGE(TAG,"TLS_HANDSHAKE_FAILED rc=%d verify_flags=%lu",rc,(unsigned long)mbedtls_ssl_get_verify_result(&t->ssl));
        goto cleanup;
    }
    ESP_LOGI(TAG,"TLS_VERIFIED version=%s",mbedtls_ssl_get_version(&t->ssl));
    mail_io_t io={.ctx=t,.read=ssl_read,.write=ssl_write,.stage=smtp_stage};
    // One absolute bound prevents trickle responses from holding the task forever.
    t->deadline=esp_timer_get_time()+120000000;
    t->session_end=t->deadline;
    if (probe_only) {
        if (mail_reply(&io)==220 && mail_write_all(&io,"EHLO esp32-mail.local\r\n") && mail_reply(&io)==250) {
            ESP_LOGI(TAG,"PROBE_OK (no AUTH, no mail)");
            // MAIL_ACCEPTED is reserved for a real DATA 250, never a probe.
            result=MAIL_PROBE_OK;
            rc=0;
            if (mail_write_all(&io,"QUIT\r\n")) (void)mail_reply(&io);
        } else rc=MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    } else {
        char user[192]={0},pass[128]={0},mime[3072]={0};
        if (b64(c->sender,user,sizeof(user)) && b64(c->authorization,pass,sizeof(pass)) &&
            make_mime(c,message,mime,sizeof(mime))) result=mail_submit(&io,c->sender,c->recipient,user,pass,mime);
        mail_secure_zero(pass,sizeof(pass)); mail_secure_zero(user,sizeof(user));
        mail_secure_zero(mime,sizeof(mime));
        // Do not issue another SMTP command after an indeterminate DATA transfer.
        if (result==MAIL_ACCEPTED) {
            t->deadline=esp_timer_get_time()+5000000;
            if (mail_write_all(&io,"QUIT\r\n")) (void)mail_reply(&io);
        }
    }
cleanup:
    if (rc) ESP_LOGE(TAG,"TLS/transport error=-0x%x; no credentials logged",(unsigned)-rc);
    // DTU is rebooted on the next explicit request; no automatic reconnect/send.
    mbedtls_ssl_free(&t->ssl); esp_crt_bundle_detach(&t->config);
    mbedtls_ssl_config_free(&t->config); mbedtls_ctr_drbg_free(&t->rng);
    mbedtls_entropy_free(&t->entropy); bootloader_random_disable();
    mail_secure_zero(t,sizeof(*t)); free(t);
    return result;
}
