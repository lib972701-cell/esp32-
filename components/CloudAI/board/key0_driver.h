#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
/* 板级共享I2C0设备，读取失败会解除武装，不把失败值解释为按键事件。 */
esp_err_t key0_driver_start(i2c_master_dev_handle_t expander);
