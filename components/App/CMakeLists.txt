set(private_config "${CMAKE_CURRENT_LIST_DIR}/../../private")
if(NOT EXISTS "${private_config}/mail_config.h")
    set(private_config "${CMAKE_CURRENT_LIST_DIR}/Boot/Defaults")
endif()
idf_component_register(SRC_DIRS "Service" "Console" "Message" "Boot" "Fall" INCLUDE_DIRS "Service" "Console" "Message" "Boot" "Fall"
    PRIV_INCLUDE_DIRS "${private_config}" REQUIRES Middlewares BSP console esp_timer imu_hal fall_core)
