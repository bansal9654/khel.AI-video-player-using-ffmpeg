cmake_minimum_required(VERSION 3.15)
project(VmixPlayer)

find_package(OpenCV REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)

add_executable(vmix_player vmix_player.cpp)

target_include_directories(vmix_player
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(vmix_player
  PRIVATE
    ${OpenCV_LIBRARY_DIRS}
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(vmix_player
  PRIVATE
    ${OpenCV_LIBS}
    ${FFMPEG_LIBRARIES}
)
