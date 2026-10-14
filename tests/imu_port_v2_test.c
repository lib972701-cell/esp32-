#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "imu_port.h"
#include "driver/i2c_master.h"
static int fail_add, fail_io, removed;
static uint8_t last_write[65];
static size_t last_size;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg, void **out) {
    assert(cfg->i2c_port == 1 && cfg->sda_io_num == 4 && cfg->scl_io_num == 5);
    assert(cfg->flags.enable_internal_pullup); *out = (void*)1; return ESP_OK;
}
esp_err_t i2c_del_master_bus(void *bus) { assert(bus == (void*)1); removed++; return ESP_OK; }
esp_err_t i2c_master_bus_add_device(void *bus, const i2c_device_config_t *cfg, void **out) {
    assert(bus == (void*)1); assert(cfg->device_address == 0x68 && cfg->scl_speed_hz == 400000);
    if (fail_add) return ESP_FAIL; *out = (void*)2; return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(void *dev) { assert(dev == (void*)2); return ESP_OK; }
esp_err_t i2c_master_transmit(void *dev, const uint8_t *data, size_t len, int timeout) {
    assert(dev == (void*)2 && timeout == 50); memcpy(last_write,data,len); last_size=len;
    return fail_io ? ESP_FAIL : ESP_OK;
}
esp_err_t i2c_master_transmit_receive(void *dev, const uint8_t *reg, size_t reglen, uint8_t *out, size_t len, int timeout) {
    assert(dev == (void*)2 && reglen == 1 && *reg == 0x75 && timeout == 50);
    memset(out,0x68,len); return fail_io ? ESP_FAIL : ESP_OK;
}
void vTaskDelay(unsigned ms) { assert(ms > 0); }
int main(void) {
    assert(imu_port_init(0,4,5,400000,0xD0) == ESP_ERR_INVALID_ARG);
    assert(imu_port_init(1,41,42,400000,0xD0) == ESP_ERR_INVALID_ARG);
    fail_add=1;
    assert(imu_port_init(1,4,5,400000,0xD0) == ESP_FAIL && !imu_port_ready() && removed==1);
    fail_add=0;
    assert(imu_port_init(1,4,5,400000,0xD0) == ESP_OK && imu_port_ready());
    assert(imu_port_bus()==1 && imu_port_sda_gpio()==4 && imu_port_scl_gpio()==5);
    uint8_t data[]={1,2}, id;
    assert(imu_port_write(NULL,0x6B,data,2)==0);
    assert(last_size==3 && last_write[0]==0x6B && last_write[1]==1 && last_write[2]==2);
    assert(imu_port_read(NULL,0x75,&id,1)==0 && id==0x68);
    fail_io=1;
    assert(imu_port_read(NULL,0x75,&id,1)==-1);
    assert(imu_port_write(NULL,0x6B,data,2)==-1);
    assert(imu_port_error_count()==2);
    assert(imu_port_write(NULL,0x6B,data,65)==-1);
    fail_io=0;
    assert(imu_port_reinit(4,5,400000,0xD0)==ESP_OK);
    assert(imu_port_read(NULL,0x75,&id,1)==0);
    puts("IMU I2C v2 API, unchanged wiring/address/timeout, failure cleanup and recovery passed");
}
