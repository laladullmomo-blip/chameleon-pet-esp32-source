# Keep Component Manager sources/checksums intact. Compile three reviewed,
# generated replacements against the original component's private includes.
idf_component_get_property(ch_adapter_dir espressif__esp_lvgl_adapter COMPONENT_DIR)
idf_component_get_property(ch_adapter_lib espressif__esp_lvgl_adapter COMPONENT_LIB)
idf_build_get_property(ch_python PYTHON)
set(ch_adapter_output "${CMAKE_BINARY_DIR}/chameleon_adapter")
set(ch_adapter_script "${CMAKE_CURRENT_LIST_DIR}/../scripts/prepare_standby_adapter.py")
execute_process(
    COMMAND "${ch_python}" "${ch_adapter_script}" "${ch_adapter_dir}" "${ch_adapter_output}"
    RESULT_VARIABLE ch_adapter_result ERROR_VARIABLE ch_adapter_error)
if(NOT ch_adapter_result EQUAL 0)
    message(FATAL_ERROR "Chameleon standby adapter generation failed: ${ch_adapter_error}")
endif()
get_target_property(ch_adapter_sources ${ch_adapter_lib} SOURCES)
set(ch_adapter_replaced 0)
set(ch_adapter_updated "")
foreach(ch_source IN LISTS ch_adapter_sources)
    if(ch_source MATCHES "(^|/)src/(adapter/esp_lv_adapter|input/esp_lv_adapter_input_touch|display/display_manager)\\.c$")
        string(REGEX REPLACE "^.*(src/(adapter/esp_lv_adapter|input/esp_lv_adapter_input_touch|display/display_manager)\\.c)$" "\\1" ch_relative "${ch_source}")
        list(APPEND ch_adapter_updated "${ch_adapter_output}/${ch_relative}")
        math(EXPR ch_adapter_replaced "${ch_adapter_replaced}+1")
    else()
        list(APPEND ch_adapter_updated "${ch_source}")
    endif()
endforeach()
if(NOT ch_adapter_replaced EQUAL 3)
    message(FATAL_ERROR "Expected exactly three adapter sources, got ${ch_adapter_replaced}")
endif()
set_property(TARGET ${ch_adapter_lib} PROPERTY SOURCES "${ch_adapter_updated}")
target_include_directories(${ch_adapter_lib} PRIVATE "${PROJECT_SOURCE_DIR}/main")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ch_adapter_script}" "${ch_adapter_dir}/src/adapter/esp_lv_adapter.c"
    "${ch_adapter_dir}/src/input/esp_lv_adapter_input_touch.c"
    "${ch_adapter_dir}/src/display/display_manager.c")
