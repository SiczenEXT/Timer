cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(counter-timer MODULE
    src/plugin-main.cpp
    src/counter_timer.cpp
)

find_package(libobs REQUIRED)
target_link_libraries(counter-timer PRIVATE OBS::libobs gdiplus gdi32)
target_compile_definitions(counter-timer PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties_plugin(counter-timer PROPERTIES OUTPUT_NAME "counter-timer")
