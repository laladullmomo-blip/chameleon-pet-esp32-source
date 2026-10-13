cmake_minimum_required(VERSION 3.16)
project(pet_ui_checks LANGUAGES C CXX)
get_filename_component(PET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PET_ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(ui_checks ui_checks.cpp
    "${PET_ROOT}/main/pet_ui.cpp" "${PET_ROOT}/main/pet_state.cpp"
    "${PET_ROOT}/main/pet_save.cpp" "${PET_ROOT}/main/pet_frames.c"
    "${PET_ROOT}/main/pet_font.c")
target_include_directories(ui_checks BEFORE PRIVATE
    "${PET_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/stubs" "${PET_ROOT}/main")
file(COPY "${PET_ROOT}/tests/stubs/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/stubs" PATTERN "lvgl.h" EXCLUDE)
target_link_libraries(ui_checks PRIVATE lvgl::lvgl)
target_compile_features(ui_checks PRIVATE cxx_std_17)
target_compile_options(ui_checks PRIVATE -Wall -Wextra -Wno-unused-variable -UNDEBUG)
