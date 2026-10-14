set(private_config "${CMAKE_CURRENT_LIST_DIR}/../../private")
if(NOT EXISTS "${private_config}/mail_config.h")
    set(private_config "${CMAKE_CURRENT_LIST_DIR}/Boot/Defaults")
endif()
idf_component_register(
    SRCS "Service/app_service.c" "Service/fall_app.c" "Vision/activity_service.c"
         "Console/app_console.c"
         "Message/location_mail.c" "Message/fall_mail.c"
         "Boot/boot_config.c" "Boot/app_config.c" "Boot/app_calib.c"
    INCLUDE_DIRS "Service" "Console" "Message" "Boot" "Vision"
    PRIV_INCLUDE_DIRS "${private_config}" REQUIRES Middlewares BSP console esp_timer)
