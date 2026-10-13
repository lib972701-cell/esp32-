/*
 * UART0 调试控制台：收集输入、注册命令并调用 Service；不直接访问业务 UART1。
 * 控制台与自动启动任务可同时存在，是否可操作由 Service 的 busy 状态决定。
 */
/*
 * UART0 调试控制台：收集输入、注册命令并调用 Service；不直接访问业务 UART1。
 * 控制台与自动启动任务可同时存在，是否可操作由 Service 的 busy 状态决定。
 */
/* 控制台：解析命令、收集隐藏输入，通过Service接口执行业务。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "app_console.h"
#include "app_service.h"
#include "mail_service.h"
#include "location_service.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
static const char *result_names[]={"FAILED","REJECTED","UNKNOWN_NO_RETRY","DATA_ACCEPTED","PROBE_OK"};
static int cmd_location(int argc, char **argv)
{
    if (argc>3 || (argc>=2 && strcmp(argv[1],"lbs") && strcmp(argv[1],"gnss")) ||
        (argc==3 && strcmp(argv[2],"show"))) {
        puts("Usage: location_get [lbs|gnss] [show]");return 1;
    }
    location_source_t source=argc>=2 && !strcmp(argv[1],"gnss") ? LOCATION_GNSS : LOCATION_LBS;
    esp_err_t e=app_service_location_request(source,argc==3);
    printf("LOCATION_REQUEST=%s\n",esp_err_to_name(e));return e==ESP_OK ? 0 : 1;
}
static int cmd_location_status(int argc, char **argv)
{
    (void)argc;(void)argv;
    location_result_t result;uint32_t age;
    esp_err_t e=location_get_last(&result,&age);
    if (e!=ESP_OK) {puts("LOCATION_STATUS unavailable_or_pending");return 1;}
    printf("LOCATION_STATUS source=%s status=%s query_age_ms=%lu elapsed_ms=%lu error=%d\n",
        result.source==LOCATION_LBS ? "LBS" : "GNSS",location_status_name(result.status),
        (unsigned long)age,(unsigned long)result.elapsed_ms,result.error_code);
    return 0;
}
static int cmd_status(int argc, char **argv) {
    (void)argc;(void)argv;app_service_status_t status;app_service_status(&status);
    printf("MAIL_STATUS wiring=%d time=%d configured=%d busy=%d result=%s heap=%lu\n",
        status.wiring,status.clock,status.configured,status.busy,
        status.result_ready ? result_names[status.result] : "NONE_OR_PENDING",(unsigned long)esp_get_free_heap_size());
    return 0;
}static int cmd_wiring(int argc, char **argv) {
    (void)argc;(void)argv;esp_err_t e=app_service_enable_uart();
    printf("MAIL_WIRING=%s TX=%d RX=%d 115200_8N1\n",esp_err_to_name(e),CONFIG_MAIL_DTU_TX_GPIO,CONFIG_MAIL_DTU_RX_GPIO);
    return e==ESP_OK ? 0 : 1;
}static int cmd_time(int argc, char **argv) {
    if (argc!=2) {puts("Usage: mail_time UNIX_SECONDS_UTC");return 1;}
    char *end;long long epoch=strtoll(argv[1],&end,10);
    if (!argv[1][0] || *end) return 1;
    esp_err_t e=app_service_set_time(epoch);
    puts(e==ESP_OK ? "MAIL_TIME_OK" : e==ESP_ERR_INVALID_STATE ? "BUSY" : "MAIL_TIME_FAILED");
    return e==ESP_OK ? 0 : 1;
}// Console handler owns stdin: linenoise is not reading or echoing these lines.
// Authorization never appears as a shell argument or in console history.
/*
 * 限时读取凭据，不经 linenoise 历史记录；调用方已将 stdin 改为非阻塞。
 * 仅接受非空可打印非空白 ASCII；非法字符或容量不足会标记整行无效。
 */
/*
 * 限时读取凭据，不经 linenoise 历史记录；调用方已将 stdin 改为非阻塞。
 * 仅接受非空可打印非空白 ASCII；非法字符或容量不足会标记整行无效。
 */
static bool hidden_line(char *out, size_t capacity)
{
    size_t n=0; bool valid=true;
    int64_t end=esp_timer_get_time()+60000000;
    while (esp_timer_get_time()<end) {
        int ch=getchar();
        if (ch==EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (ch=='\n' || ch=='\r') { out[n]=0; return valid && n>0; }
        if (ch<33 || ch>126 || n+1>=capacity) valid=false;
        else out[n++]=(char)ch;
    }
    out[n]=0; return false;
}
/*
 * 先预占 Service，再收集邮箱和隐藏授权码；恢复 stdin 标志后结束配置会话。
 * 输入失败同样调用 config_finish(NULL) 释放预占，局部凭据始终清零。
 */
/*
 * 先预占 Service，再收集邮箱和隐藏授权码；恢复 stdin 标志后结束配置会话。
 * 输入失败同样调用 config_finish(NULL) 释放预占，局部凭据始终清零。
 */
static int cmd_setup(int argc, char **argv) {
    (void)argc;(void)argv;
    if (app_service_config_begin()!=ESP_OK) {puts("BUSY");return 1;}
    int flags=fcntl(fileno(stdin),F_GETFL);
    if (flags<0 || fcntl(fileno(stdin),F_SETFL,flags | O_NONBLOCK)<0) {
        (void)app_service_config_finish(NULL);puts("MAIL_CONFIG_INVALID");return 1;
    }
    mail_credentials_t c={0};
    puts("INPUT_SENDER");fflush(stdout);bool ok=hidden_line(c.sender,sizeof(c.sender));
    if (ok) {puts("INPUT_RECIPIENT");fflush(stdout);ok=hidden_line(c.recipient,sizeof(c.recipient));}
    if (ok) {puts("INPUT_AUTHORIZATION_HIDDEN");fflush(stdout);ok=hidden_line(c.authorization,sizeof(c.authorization));}
    (void)fcntl(fileno(stdin),F_SETFL,flags);
    esp_err_t e=app_service_config_finish(ok ? &c : NULL);
    mail_secure_zero(&c,sizeof(c));
    puts(e==ESP_OK ? "MAIL_CONFIG_READY_RAM_ONLY" : "MAIL_CONFIG_INVALID");return e==ESP_OK ? 0 : 1;
}static int cmd_clear(int argc, char **argv) {
    (void)argc;(void)argv;esp_err_t e=app_service_clear();
    puts(e==ESP_OK ? "MAIL_CLEARED" : "BUSY");return e==ESP_OK ? 0 : 1;
}static int cmd_send(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t e=mail_request_test();
    printf("MAIL_REQUEST=%s\n",esp_err_to_name(e)); return e==ESP_OK ? 0 : 1;
}
static int cmd_probe(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t e=app_service_probe();
    printf("MAIL_REQUEST=%s\n",esp_err_to_name(e)); return e==ESP_OK ? 0 : 1;
}
static int cmd_send_location(int argc, char **argv)
{
    if (argc>2 || (argc==2 && strcmp(argv[1],"lbs") && strcmp(argv[1],"gnss"))) {
        puts("Usage: mail_send_location [lbs|gnss]");return 1;
    }
    location_source_t source=argc==2 && !strcmp(argv[1],"gnss") ? LOCATION_GNSS : LOCATION_LBS;
    esp_err_t e=mail_request_with_location("ESP32-S3 定位邮件测试",
        "这是定位邮件测试，并非真实求助。以下位置来自本次模块查询。",source);
    printf("MAIL_REQUEST=%s\n",esp_err_to_name(e));return e==ESP_OK ? 0 : 1;
}
/*
 * 将命令名、帮助文本和处理函数注册到 ESP-IDF Console。
 */
/*
 * 将命令名、帮助文本和处理函数注册到 ESP-IDF Console。
 */
static void register_command(const char *name, const char *help, esp_console_cmd_func_t fn)
{
    esp_console_cmd_t cmd={.command=name,.help=help,.func=fn};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
static int cmd_clock(int argc,char **argv) {
    (void)argc;(void)argv;
    esp_err_t e=app_service_clock_request();
    printf("DTU_TIME_REQUEST=%s\n",esp_err_to_name(e));return e==ESP_OK ? 0 : 1;
}
/*
 * 创建 UART0 REPL 并注册状态、校时、定位和邮件命令。
 * 末尾历史提示是静态文本；自动启动行为以 Boot 配置和 Service 代码为准。
 */
/*
 * 创建 UART0 REPL 并注册状态、校时、定位和邮件命令。
 * 末尾历史提示是静态文本；自动启动行为以 Boot 配置和 Service 代码为准。
 */
esp_err_t app_console_start(void)
{
    esp_console_repl_t *repl=NULL;
    esp_console_repl_config_t cfg=ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt="mail> "; cfg.max_cmdline_length=160; cfg.history_save_path=NULL;
    esp_console_dev_uart_config_t uart=ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart,&cfg,&repl));
    register_command("mail_status","Show safe state and free heap",cmd_status);
    register_command("location_get","Query LBS(default) or GNSS; optional show prints coordinates locally",cmd_location);
    register_command("location_status","Show last location result and query age, without coordinates",cmd_location_status);
    register_command("mail_wiring_ok","Confirm 3.3V UART wiring and no LCD/camera; enable UART1",cmd_wiring);
    register_command("mail_time","Set accurate current UTC Unix seconds before TLS",cmd_time);
    register_command("dtu_time_sync","Read DTU network clock without computer time",cmd_clock);
    register_command("mail_setup","Enter sender, recipient and hidden QQ authorization (RAM only)",cmd_setup);
    register_command("mail_clear","Erase RAM credentials",cmd_clear);
    register_command("mail_probe","Reboot DTU; verify TLS and SMTP EHLO, no email",cmd_probe);
    register_command("mail_send_test","Reboot DTU and send exactly one test email; no automatic retry",cmd_send);
    register_command("mail_send_location","Query LBS(default)/GNSS then email location or failure note, exactly once",cmd_send_location);
    puts("ESP32-S3 serial 4G mail. No automatic send. UART1 is disabled until mail_wiring_ok.");
    return esp_console_start_repl(repl);
}
