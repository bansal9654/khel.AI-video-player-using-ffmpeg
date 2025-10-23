cmake_minimum_required(VERSION 3.15)
project(VmixPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)

add_executable(vmix_player vmix_player.cpp)

target_include_directories(vmix_player
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

target_compile_features(vmix_player PRIVATE cxx_std_17)
target_compile_options(vmix_player PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4100>
)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

target_compile_definitions(vmix_player PRIVATE
  $<$<CONFIG:Debug>:DEBUG>
)

target_link_libraries(vmix_player
  PRIVATE
    ${OpenCV_LIBS}
    ${FFMPEG_LIBRARIES}
)

if(FFMPEG_LDFLAGS)
  target_link_options(vmix_player PRIVATE ${FFMPEG_LDFLAGS})
endif()

set_target_properties(vmix_player PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)