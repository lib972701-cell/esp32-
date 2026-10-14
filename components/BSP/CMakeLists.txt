if("$ENV{SAMPLE_CLOUD_AI_BUILD}" STREQUAL "1")
    set(imu_transport_sources "I2C/idf5/imu_port_v2.c" "I2C/idf5/imu_bus_v2.c")
    set(imu_transport_requires esp_driver_i2c)
else()
    set(imu_transport_sources "MPU6050/imu_port.c" "MPU6050/imu_bus.c")
endif()

# 两种构建共用同一份DTU/MPU6050驱动，只替换I2C接口适配。
idf_component_register(
    SRCS "DTU/dtu_uart.c" "DTU/dtu_frame.c" "Vision/vision_uart.c"
         "MPU6050/imu_mpu6050.c" "MPU6050/imu_device.c"
         ${imu_transport_sources}
    INCLUDE_DIRS "DTU" "MPU6050/include" "Vision"
    PRIV_INCLUDE_DIRS "MPU6050"
    REQUIRES driver esp_timer ${imu_transport_requires})
