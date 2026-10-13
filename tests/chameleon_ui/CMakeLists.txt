cmake_minimum_required(VERSION 3.16)
project(chameleon_ui_checks LANGUAGES C CXX)
get_filename_component(CH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CH_ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(chameleon_ui_checks ui_checks.cpp
    "${CH_ROOT}/main/chameleon_ui.cpp" "${CH_ROOT}/main/chameleon_state.cpp" "${CH_ROOT}/main/chameleon_forest.cpp"
    "${CH_ROOT}/main/chameleon_assets.c" "${CH_ROOT}/main/chameleon_font.c"
    "${CH_ROOT}/main/chameleon_mood_color.cpp" "${CH_ROOT}/main/chameleon_games.cpp" "${CH_ROOT}/main/chameleon_alarm.cpp")
target_sources(chameleon_ui_checks PRIVATE "${CH_ROOT}/main/chameleon_display.cpp")
target_include_directories(chameleon_ui_checks BEFORE PRIVATE
    "${CH_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${CMAKE_CURRENT_BINARY_DIR}/stubs" "${CH_ROOT}/main")
file(COPY "${CH_ROOT}/tests/stubs/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/stubs" PATTERN "lvgl.h" EXCLUDE)
target_link_libraries(chameleon_ui_checks PRIVATE lvgl::lvgl)
target_compile_features(chameleon_ui_checks PRIVATE cxx_std_17)
target_compile_options(chameleon_ui_checks PRIVATE -Wall -Wextra -Wno-unused-variable -UNDEBUG)
