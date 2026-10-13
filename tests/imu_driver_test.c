#include <stdio.h>
#include <string.h>
#include "imu_mpu6050.h"
#include "imu_port.h"
static unsigned write_count, fail_write;
static uint8_t divider;
int64_t esp_timer_get_time(void) {return 1000000;}
void vTaskDelay(unsigned ticks) {(void)ticks;}
int32_t imu_port_read(void *h, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)h; memset(buf,0,len);
    if (reg==0x75) buf[0]=0x68;
    return 0;
}
int32_t imu_port_write(void *h, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)h;(void)len;
    if (reg==0x19) divider=buf[0];
    return ++write_count==fail_write ? -1 : 0;
}
#define CHECK(x) do {if (!(x)) {printf("FAIL line %d write %u\n",__LINE__,fail_write);return 1;}} while (0)
int main(void)
{
    for (fail_write=1;fail_write<=7;fail_write++) {
        write_count=0;
        CHECK(imu_mpu6050_init()!=ESP_OK);
        CHECK(!imu_mpu6050_present());
    }
    fail_write=0;write_count=0;
    CHECK(imu_mpu6050_init()==ESP_OK);
    CHECK(imu_mpu6050_present());
    CHECK(write_count==7 && divider==7);
    puts("PASS: every MPU6050 init write failure stays unavailable; recovery succeeds.");
    return 0;
}
