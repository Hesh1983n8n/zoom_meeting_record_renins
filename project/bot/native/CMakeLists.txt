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
set(ZOOM_SDK_INC "${ZOOM_SDK_DIR}/include")
set(ZOOM_SDK_LIB "${ZOOM_SDK_DIR}/lib")

target_include_directories(zoom_bot_recorder PRIVATE "${ZOOM_SDK_INC}")
target_link_directories(zoom_bot_recorder PRIVATE "${ZOOM_SDK_LIB}")

# IMPORTANT:
# Do NOT use system Qt via find_package(Qt5). Zoom SDK ships its own Qt in ${ZOOM_SDK_LIB}.
# Link explicitly against SDK-provided libraries.

# Link order matters: meetingsdk first, then its dependencies.
target_link_libraries(zoom_bot_recorder PRIVATE
  meetingsdk

  # Zoom-bundled Qt (must come from /app/zoom_sdk/lib)
  Qt5Core
  Qt5Network
  Qt5DBus
  Qt5Gui
  Qt5Widgets
  Qt5XcbQpa
  Qt5X11Extras

  # System libs
  pthread
  dl
)

set_target_properties(zoom_bot_recorder PROPERTIES
  BUILD_RPATH "${ZOOM_SDK_LIB}"
  INSTALL_RPATH "${ZOOM_SDK_LIB}"
)
