"""One-time migration; original sources are retained in history before restructuring."""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'main'
BACKUP = ROOT / 'history/20261011-before-layer-refactor'
if BACKUP.exists():
    raise SystemExit('Migration already started; refusing to overwrite its backup.')
shutil.copytree(MAIN, BACKUP / 'main')
shutil.copy2(ROOT / 'scripts/test.ps1', BACKUP / 'test.ps1')
old = (MAIN / 'main.c').read_text(encoding='utf-8')
link = (MAIN / 'mail_link.c').read_text(encoding='utf-8')

def write(path, text):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8')

def function(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}', start)+3]

def move(name, directory):
    src, dst = MAIN / name, ROOT / directory / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    assert src.resolve().is_relative_to(ROOT) and dst.resolve().is_relative_to(ROOT)
    shutil.move(str(src), str(dst))

# BSP owns UART hardware; it has no mail or location dependency.
uart_functions = link[link.index('esp_err_t mail_link_init'):link.index('location_result_t mail_link_location')]
uart_functions = uart_functions.replace('mail_link_init(', 'dtu_uart_init(').replace('static bool uart_ok(', 'bool dtu_uart_ok(').replace('uart_ok(', 'dtu_uart_ok(')
uart_functions = uart_functions.replace('dtu_dtu_uart_ok', 'dtu_uart_ok').replace('static bool raw_write(', 'bool dtu_uart_write(').replace('static bool raw_line(', 'bool dtu_uart_read_line(')
uart_functions = uart_functions.replace('location_ascii_byte_valid(', 'dtu_frame_byte_valid(')
write('components/BSP/DTU/dtu_uart.c', '''#include "dtu_uart.h"
#include "dtu_frame.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#define DTU_UART UART_NUM_1
_Static_assert(CONFIG_MAIL_DTU_TX_GPIO != CONFIG_MAIL_DTU_RX_GPIO,"TX and RX must differ");
static const char *TAG="dtu_uart";
static QueueHandle_t uart_events;
static bool installed;
''' + uart_functions + '''
bool dtu_uart_ready(void) { return installed; }
void dtu_uart_flush(void) {
    if (!installed) return;
    uart_flush_input(DTU_UART);xQueueReset(uart_events);
}
int dtu_uart_read(unsigned char *buf, size_t n, unsigned timeout_ms) {
    if (!installed || !dtu_uart_ok()) return -1;
    return uart_read_bytes(DTU_UART,buf,n,pdMS_TO_TICKS(timeout_ms));
}
''')
write('components/BSP/DTU/dtu_uart.h', '''#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
/* UART1, board-specific pins in Kconfig. Single application worker owns all calls. */
esp_err_t dtu_uart_init(void);
bool dtu_uart_ready(void);
bool dtu_uart_ok(void);
void dtu_uart_flush(void);
bool dtu_uart_write(const unsigned char *buf, size_t n);
int dtu_uart_read(unsigned char *buf, size_t n, unsigned timeout_ms);
/* Complete ASCII CRLF line, returned without CRLF. Deadline is monotonic microseconds. */
bool dtu_uart_read_line(char *line, size_t capacity, int64_t deadline);
''')
write('components/BSP/DTU/dtu_frame.h', '#pragma once\n#include <stdbool.h>\nbool dtu_frame_byte_valid(unsigned char byte);\n')
write('components/BSP/DTU/dtu_frame.c', '''#include "dtu_frame.h"
bool dtu_frame_byte_valid(unsigned char byte) {
    return byte=='\\r' || byte=='\\n' || (byte>=32 && byte<=126);
}
''')

def transport(text):
    text = text.replace('if (!installed)', 'if (!dtu_uart_ready())').replace('if (!installed ||', 'if (!dtu_uart_ready() ||')
    text = text.replace('uart_flush_input(DTU_UART);xQueueReset(uart_events);', 'dtu_uart_flush();')
    text = text.replace('uart_flush_input(DTU_UART); xQueueReset(uart_events);', 'dtu_uart_flush();')
    text = text.replace('uart_flush_input(DTU_UART);\n    xQueueReset(uart_events);', 'dtu_uart_flush();')
    text = text.replace('raw_write(', 'dtu_uart_write(').replace('raw_line(', 'dtu_uart_read_line(').replace('uart_ok(', 'dtu_uart_ok(')
    return text.replace('uart_read_bytes(DTU_UART,buf,n,pdMS_TO_TICKS(100))', 'dtu_uart_read(buf,n,100)')

location = transport(function(link, 'location_result_t mail_link_location')).replace('mail_link_location(', 'location_query(')
write('components/Middlewares/LOCATION/location_query.c', '''#include "location_query.h"
#include "dtu_uart.h"
#include "esp_timer.h"
#include <string.h>
''' + location + '\n')
write('components/Middlewares/LOCATION/location_query.h', '''#pragma once
#include "location_protocol.h"
/* Must run exclusively in the application's shared UART worker. */
location_result_t location_query(location_source_t source);
''')
tls = link[link.index('typedef struct {'):link.index('void mail_secure_zero')]
mail = transport(link[link.index('static bool prepare_dtu'):])
mail_headers = link[:link.index('#define DTU_UART')].replace('#include "driver/uart.h"\n', '')
write('components/Middlewares/MAIL/mail_link.c', mail_headers + '#include "dtu_uart.h"\n#include "location_query.h"\nstatic const char *TAG="mail_link";\n' + tls + function(link, 'void mail_secure_zero') + '''
/* Compatibility entry points; hardware and positioning implementations live separately. */
esp_err_t mail_link_init(void) { return dtu_uart_init(); }
location_result_t mail_link_location(location_source_t source) { return location_query(source); }
''' + mail)
for name in ['mail_protocol.c', 'mail_protocol.h', 'mail_link.h']:
    move(name, 'components/Middlewares/MAIL')
for name in ['location_protocol.c', 'location_protocol.h']:
    move(name, 'components/Middlewares/LOCATION')
for name in ['location_mail.c', 'location_mail.h']:
    move(name, 'components/App/Message')
for name in ['mail_service.h', 'location_service.h']:
    move(name, 'components/App/Service')
move('Kconfig.projbuild', 'components/BSP')
# The removed helpers were implementation details, now owned by BSP.
p = ROOT / 'components/Middlewares/LOCATION/location_protocol.c'
s = p.read_text(encoding='utf-8')
s = s.replace(function(s, 'bool location_ascii_byte_valid')+'\n', '')
p.write_text(s, encoding='utf-8')
p = ROOT / 'components/Middlewares/LOCATION/location_protocol.h'
p.write_text(p.read_text(encoding='utf-8').replace('bool location_ascii_byte_valid(unsigned char byte);\n', ''), encoding='utf-8')

# App service owns state/queue/credentials; console obtains snapshots via interfaces.
service = old[old.index('static const char *TAG'):old.index('static int cmd_location(')]
service = service.replace('mail_link_location(', 'location_query(').replace('static esp_err_t request_location(', 'esp_err_t app_service_location_request(').replace('request_location(source,false)', 'app_service_location_request(source,false)')
write('components/App/Service/app_service.h', '''#pragma once
#include <stdint.h>
#include "mail_link.h"
typedef struct {
    bool wiring, clock, configured, busy, result_ready;
    mail_result_t result;
} app_service_status_t;
esp_err_t app_service_start(void);
void app_service_status(app_service_status_t *out);
esp_err_t app_service_enable_uart(void);
esp_err_t app_service_set_time(int64_t epoch);
esp_err_t app_service_config_begin(void);
/* NULL cancels setup and clears credentials. Caller must have reserved setup. */
esp_err_t app_service_config_finish(const mail_credentials_t *credentials);
esp_err_t app_service_clear(void);
esp_err_t app_service_probe(void);
esp_err_t app_service_location_request(location_source_t source, bool show);
''')
write('components/App/Service/app_service.c', '''#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "app_service.h"
#include "mail_service.h"
#include "location_service.h"
#include "location_query.h"
#include "location_mail.h"
#include "dtu_uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
''' + service + '''
esp_err_t app_service_start(void) {
    if (state_lock) return ESP_ERR_INVALID_STATE;
    state_lock=xSemaphoreCreateMutex();jobs=xQueueCreate(1,sizeof(mail_job_t));
    if (!state_lock || !jobs || xTaskCreate(worker,"dtu_worker",16384,NULL,5,NULL)!=pdPASS) {
        if (jobs) vQueueDelete(jobs);
        if (state_lock) vSemaphoreDelete(state_lock);
        jobs=NULL;state_lock=NULL;return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void app_service_status(app_service_status_t *out) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    *out=(app_service_status_t){wiring_ok,clock_set,configured,busy,mail_result_ready,last_result};
    xSemaphoreGive(state_lock);
}
esp_err_t app_service_enable_uart(void) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    esp_err_t e=busy ? ESP_ERR_INVALID_STATE : dtu_uart_init();
    if (e==ESP_OK) wiring_ok=true;
    xSemaphoreGive(state_lock);return e;
}
esp_err_t app_service_set_time(int64_t epoch) {
    if (epoch<1735689600LL || epoch>4102444800LL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (busy) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    struct timeval tv={.tv_sec=(time_t)epoch,.tv_usec=0};
    clock_set=settimeofday(&tv,NULL)==0;
    esp_err_t e=clock_set ? ESP_OK : ESP_FAIL;
    xSemaphoreGive(state_lock);return e;
}
static bool setup_reserved;
esp_err_t app_service_config_begin(void) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (busy) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    busy=true;setup_reserved=true;xSemaphoreGive(state_lock);return ESP_OK;
}
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
esp_err_t app_service_clear(void) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if (busy) {xSemaphoreGive(state_lock);return ESP_ERR_INVALID_STATE;}
    mail_secure_zero(&credentials,sizeof(credentials));configured=false;
    xSemaphoreGive(state_lock);return ESP_OK;
}
esp_err_t app_service_probe(void) { return request(true,NULL,NULL); }
''')

console = old[old.index('static int cmd_location('):old.index('void app_main(')]
console = console.replace('request_location(source,argc==3)', 'app_service_location_request(source,argc==3)').replace('request(true,NULL,NULL)', 'app_service_probe()')
def replace_func(signature, replacement):
    global console
    console = console.replace(function(console, signature), replacement)
replace_func('static int cmd_status(', '''static int cmd_status(int argc, char **argv) {
    (void)argc;(void)argv;app_service_status_t status;app_service_status(&status);
    printf("MAIL_STATUS wiring=%d time=%d configured=%d busy=%d result=%s heap=%lu\\n",
        status.wiring,status.clock,status.configured,status.busy,
        status.result_ready ? result_names[status.result] : "NONE_OR_PENDING",(unsigned long)esp_get_free_heap_size());
    return 0;
}''')
replace_func('static int cmd_wiring(', '''static int cmd_wiring(int argc, char **argv) {
    (void)argc;(void)argv;esp_err_t e=app_service_enable_uart();
    printf("MAIL_WIRING=%s TX=%d RX=%d 115200_8N1\\n",esp_err_to_name(e),CONFIG_MAIL_DTU_TX_GPIO,CONFIG_MAIL_DTU_RX_GPIO);
    return e==ESP_OK ? 0 : 1;
}''')
replace_func('static int cmd_time(', '''static int cmd_time(int argc, char **argv) {
    if (argc!=2) {puts("Usage: mail_time UNIX_SECONDS_UTC");return 1;}
    char *end;long long epoch=strtoll(argv[1],&end,10);
    if (!argv[1][0] || *end) return 1;
    esp_err_t e=app_service_set_time(epoch);
    puts(e==ESP_OK ? "MAIL_TIME_OK" : e==ESP_ERR_INVALID_STATE ? "BUSY" : "MAIL_TIME_FAILED");
    return e==ESP_OK ? 0 : 1;
}''')
replace_func('static int cmd_setup(', '''static int cmd_setup(int argc, char **argv) {
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
}''')
replace_func('static int cmd_clear(', '''static int cmd_clear(int argc, char **argv) {
    (void)argc;(void)argv;esp_err_t e=app_service_clear();
    puts(e==ESP_OK ? "MAIL_CLEARED" : "BUSY");return e==ESP_OK ? 0 : 1;
}''')
startup = old[old.index('    esp_console_repl_t *repl=NULL;'):]
startup = startup.replace('    ESP_ERROR_CHECK(esp_console_start_repl(repl));', '    return esp_console_start_repl(repl);')
write('components/App/Console/app_console.c', '''#include <stdio.h>
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
''' + console + 'esp_err_t app_console_start(void)\n{\n' + startup)
write('components/App/Console/app_console.h', '#pragma once\n#include "esp_err.h"\nesp_err_t app_console_start(void);\n')
write('main/main.c', '''#include "app_service.h"
#include "app_console.h"
#include "esp_err.h"

void app_main(void)
{
    ESP_ERROR_CHECK(app_service_start());
    ESP_ERROR_CHECK(app_console_start());
}
''')
write('main/CMakeLists.txt', 'idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES App)\n')
write('components/BSP/CMakeLists.txt', 'idf_component_register(SRC_DIRS "DTU" INCLUDE_DIRS "DTU" REQUIRES driver esp_timer)\n')
write('components/Middlewares/CMakeLists.txt', 'idf_component_register(SRC_DIRS "MAIL" "LOCATION" INCLUDE_DIRS "MAIL" "LOCATION" REQUIRES BSP mbedtls esp_timer bootloader_support)\n')
write('components/App/CMakeLists.txt', 'idf_component_register(SRC_DIRS "Service" "Console" "Message" INCLUDE_DIRS "Service" "Console" "Message" REQUIRES Middlewares BSP console esp_timer)\n')
# Preserve the two originals not moved because their implementations were split.
for name in ['mail_link.c']:
    move(name, 'history/20261011-before-layer-refactor/originals')
# Host tests follow new source locations and also cover BSP frame validation.
p = ROOT / 'tests/location_test.c';s=p.read_text(encoding='utf-8').replace('#include "location_protocol.h"', '#include "location_protocol.h"\n#include "dtu_frame.h"').replace('location_ascii_byte_valid(', 'dtu_frame_byte_valid(');p.write_text(s,encoding='utf-8')
p = ROOT / 'scripts/test.ps1';s=p.read_text(encoding='utf-8')
s=s.replace('-I main', '-I components/Middlewares/MAIL -I components/Middlewares/LOCATION -I components/App/Message -I components/BSP/DTU')
s=s.replace('main/mail_protocol.c', 'components/Middlewares/MAIL/mail_protocol.c').replace('main/location_protocol.c', 'components/Middlewares/LOCATION/location_protocol.c').replace('main/location_mail.c', 'components/App/Message/location_mail.c')
s=s.replace('tests/location_test.c components/Middlewares/LOCATION/location_protocol.c', 'tests/location_test.c components/Middlewares/LOCATION/location_protocol.c components/BSP/DTU/dtu_frame.c')
p.write_text(s,encoding='utf-8')
print('Layer migration completed; original main and test runner retained in history.')
