idf_component_register(
    SRCS "MAIL/mail_link.c" "MAIL/mail_protocol.c" "Vision/vision_session.c"
         "LOCATION/location_query.c" "LOCATION/location_protocol.c"
         "TIME/dtu_time_query.c" "TIME/dtu_time_parse.c"
         "FALL/attitude.c" "FALL/imu_calib.c" "FALL/fall_detector.c" "FALL/help_policy.c"
    INCLUDE_DIRS "MAIL" "LOCATION" "TIME" "FALL/include" "Vision"
    REQUIRES BSP mbedtls esp_timer bootloader_support)
