#include "imu_device.h"

#include "imu_mpu6050.h"

esp_err_t imu_device_init(void)
{
    return imu_mpu6050_init();
}

bool imu_device_present(void)
{
    return imu_mpu6050_present();
}

esp_err_t imu_device_check_alive(void)
{
    return imu_mpu6050_check_alive();
}

int imu_device_read(imu_device_cb_t cb, void *arg)
{
    return imu_mpu6050_read(cb, arg);
}

uint64_t imu_device_now_us(void)
{
    return imu_mpu6050_now_us();
}

const char *imu_device_name(void)
{
    return "MPU-6050 (GY-521)";
}
