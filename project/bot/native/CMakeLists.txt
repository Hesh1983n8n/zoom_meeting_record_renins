cmake_minimum_required(VERSION 3.16)
project(zoom_bot_recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(zoom_bot_recorder main.cpp)

option(ENABLE_RAW_AUDIO "Enable raw audio" ON)
if(ENABLE_RAW_AUDIO)
  target_compile_definitions(zoom_bot_recorder PRIVATE ENABLE_RAW_AUDIO=1)
endif()

set(ZOOM_SDK_DIR "/app/zoom_sdk")
set(ZOOM_SDK_LIB "/app/zoom_sdk/lib")
target_include_directories(zoom_bot_recorder PRIVATE "/app/zoom_sdk/include")
link_directories("${ZOOM_SDK_LIB}")

find_package(Qt5 REQUIRED COMPONENTS Core)

file(GLOB_RECURSE ZOOMSDK_LIBS
  "${ZOOM_SDK_DIR}/*.so"
  "${ZOOM_SDK_DIR}/*.a"
)
if(NOT ZOOMSDK_LIBS)
  message(FATAL_ERROR "Zoom SDK libraries (.so/.a) not found under ${ZOOM_SDK_DIR}")
endif()

message(STATUS "Found Zoom SDK libs: ${ZOOMSDK_LIBS}")

target_link_libraries(zoom_bot_recorder PRIVATE
  ${ZOOMSDK_LIBS}
  Qt5::Core
  pthread
  dl
)

set_target_properties(zoom_bot_recorder PROPERTIES
  BUILD_RPATH "${ZOOM_SDK_LIB}"
  INSTALL_RPATH "${ZOOM_SDK_LIB}"
)
