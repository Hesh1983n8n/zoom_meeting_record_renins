cmake_minimum_required(VERSION 3.16)
project(zoom_bot_recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(zoom_bot_recorder main.cpp)

set(ZOOM_SDK_LIB "/app/zoom_sdk/lib")
target_include_directories(zoom_bot_recorder PRIVATE "/app/zoom_sdk/include")
link_directories("${ZOOM_SDK_LIB}")

find_library(ZOOMSDK_LIB zoomsdk PATHS "${ZOOM_SDK_LIB}" NO_DEFAULT_PATH)
if(NOT ZOOMSDK_LIB)
  message(FATAL_ERROR "Zoom SDK library not found in ${ZOOM_SDK_LIB}")
endif()

target_link_libraries(zoom_bot_recorder PRIVATE
  ${ZOOMSDK_LIB}
  pthread
  dl
)

set_target_properties(zoom_bot_recorder PROPERTIES
  BUILD_RPATH "${ZOOM_SDK_LIB}"
  INSTALL_RPATH "${ZOOM_SDK_LIB}"
)
