cmake_minimum_required(VERSION 3.16)
project(yunlu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL)

# FFmpeg 开发环境无 pkg-config(.pc) 文件（rockchip 固件只带头文件+动态库），
# 直接按头文件+库名定位。
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    PATHS /usr/include
    PATH_SUFFIXES ffmpeg)
find_library(AVCODEC_LIB     NAMES avcodec)
find_library(AVFORMAT_LIB    NAMES avformat)
find_library(AVUTIL_LIB      NAMES avutil)
find_library(AVFILTER_LIB    NAMES avfilter)
find_library(SWSCALE_LIB     NAMES swscale)
find_library(SWRESAMPLE_LIB  NAMES swresample)

if(NOT FFMPEG_INCLUDE_DIR OR NOT AVCODEC_LIB)
    message(FATAL_ERROR "未找到 FFmpeg 头文件或 libavcodec.so。请先运行 scripts/link_ffmpeg.sh 创建符号链接。")
endif()

# RGA (Rockchip 2D 加速) 用于任意格式 -> NV12 的硬件转换
find_path(RGA_INCLUDE_DIR rga/im2d.h PATHS /usr/include)
find_library(RGA_LIB NAMES rga)
if(RGA_INCLUDE_DIR AND RGA_LIB)
    message(STATUS "Found librga: ${RGA_LIB} (include ${RGA_INCLUDE_DIR})")
else()
    message(WARNING "未找到 librga，格式转换将回退到软件 swscale")
endif()

add_executable(yunlu
    src/main.cpp
    src/common.h
    src/capture/hdmi_capture.cpp
    src/capture/hdmi_capture.h
    src/encoder/vpu_encoder.cpp
    src/encoder/vpu_encoder.h
    src/output/media_output.cpp
    src/output/media_output.h
    src/display/video_widget.cpp
    src/display/video_widget.h
    src/display/video_renderer.cpp
    src/display/video_renderer.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/controller.cpp
    src/controller.h
)

target_link_libraries(yunlu PRIVATE
    Qt5::Widgets
    Qt5::OpenGL
    ${AVCODEC_LIB} ${AVFORMAT_LIB} ${AVUTIL_LIB} ${AVFILTER_LIB}
    ${SWSCALE_LIB} ${SWRESAMPLE_LIB}
    ${RGA_LIB}
    dl pthread m
)

target_include_directories(yunlu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/config)
target_include_directories(yunlu PRIVATE ${FFMPEG_INCLUDE_DIR})
if(RGA_INCLUDE_DIR)
    target_include_directories(yunlu PRIVATE ${RGA_INCLUDE_DIR})
    target_compile_definitions(yunlu PRIVATE YUNLU_HAVE_RGA=1)
endif()

install(TARGETS yunlu RUNTIME DESTINATION bin)
