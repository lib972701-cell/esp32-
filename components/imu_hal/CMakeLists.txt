idf_component_register(
    SRCS "imu_device.c" "imu_mpu6050.c" "imu_port.c" "imu_bus.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES fall_core
    PRIV_REQUIRES driver esp_timer)
