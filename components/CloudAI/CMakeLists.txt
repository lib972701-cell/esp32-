if("$ENV{SAMPLE_CLOUD_AI_BUILD}" STREQUAL "1")
    idf_component_register(
        SRCS
            # 对话生命周期及业务入口
            "control/cloud_ai.cc" "control/application.cc"
            # DNESP32S3板级适配：按键、Codec及Wi-Fi启动
            "board/board.cc" "board/key0_driver.cc" "board/es8388_audio_codec.cc"
            # 音频队列、采播及Opus处理
            "audio/audio_codec.cc" "audio/audio_service.cc" "audio/processors/no_audio_processor.cc"
            # 服务发现、激活及云端传输
            "network/cloud_bootstrap.cc" "network/protocol.cc"
            "network/mqtt_protocol.cc" "network/websocket_protocol.cc"
            # NVS设置、设备信息及语言字符串
            "support/settings.cc" "support/system_info.cc"
        INCLUDE_DIRS "control" "board" "audio" "network" "support"
        REQUIRES App esp_psram driver esp_timer esp_wifi esp_event esp_netif nvs_flash app_update esp_partition
                 json mbedtls spi_flash efuse esp_hw_support esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_spi
                 "78__esp-ml307" "78__esp-wifi-connect" "78__esp-opus" "78__esp-opus-encoder" "espressif__esp_codec_dev")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_NAME="atk-dnesp32s3")
else()
    idf_component_register()
endif()

