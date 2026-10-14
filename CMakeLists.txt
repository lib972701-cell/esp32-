# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "2.0.3-instrument")
# 两种配置均只编译main的实际依赖，避免把无关SDK组件带入工程。
set(COMPONENTS main)

if(CLOUD_AI_BUILD)
    # 同一构建开关同时控制组件发现与源码选择（早期依赖展开尚无sdkconfig）。
    set(ENV{SAMPLE_CLOUD_AI_BUILD} "1")
    # 固定使用已核验的小智本地组件，构建时不在线升级依赖。
    foreach(cloud_component "78__esp-ml307" "78__esp-wifi-connect" "78__esp-opus"
                            "78__esp-opus-encoder" "espressif__esp_codec_dev")
        list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/managed_components/${cloud_component}")
    endforeach()
    set(ENV{IDF_COMPONENT_MANAGER} "0")
else()
    set(ENV{SAMPLE_CLOUD_AI_BUILD} "0")
endif()

# Application, middleware and BSP are registered from components/.
add_compile_options(-fdiagnostics-color=always)
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main)
if(CLOUD_AI_BUILD)
    idf_component_get_property(cloud_encoder 78__esp-opus-encoder COMPONENT_LIB)
    idf_component_get_property(cloud_opus 78__esp-opus COMPONENT_LIB)
    target_link_libraries(${cloud_encoder} PUBLIC ${cloud_opus})
endif()
