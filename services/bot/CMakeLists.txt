cmake_minimum_required(VERSION 3.16)
project(zoom_bot_recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED SDK_ROOT)
  set(SDK_ROOT "/opt/sdk")
endif()
message(STATUS "SDK_ROOT=${SDK_ROOT}")

if(EXISTS "${SDK_ROOT}/h")
  set(ZOOM_SDK_INCLUDE_DIR "${SDK_ROOT}/h")
else()
  set(ZOOM_SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/services/bot/third_party/zoom_meeting_sdk/h")
endif()
message(STATUS "Using Zoom SDK headers from: ${ZOOM_SDK_INCLUDE_DIR}")

find_package(Threads REQUIRED)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(XCB REQUIRED xcb)
pkg_check_modules(XCB_RANDR REQUIRED xcb-randr)
pkg_check_modules(XCB_SHM REQUIRED xcb-shm)
pkg_check_modules(XCB_XFIXES REQUIRED xcb-xfixes)
pkg_check_modules(XCB_SHAPE REQUIRED xcb-shape)
pkg_check_modules(XCB_IMAGE REQUIRED xcb-image)
pkg_check_modules(XCB_KEYSYMS REQUIRED xcb-keysyms)
pkg_check_modules(XCB_XTEST REQUIRED xcb-xtest)
pkg_check_modules(XCB_UTIL REQUIRED xcb-util)

include_directories(
  ${ZOOM_SDK_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DBUS_INCLUDE_DIRS}
  ${XCB_INCLUDE_DIRS}
  ${XCB_RANDR_INCLUDE_DIRS}
  ${XCB_SHM_INCLUDE_DIRS}
  ${XCB_XFIXES_INCLUDE_DIRS}
  ${XCB_SHAPE_INCLUDE_DIRS}
  ${XCB_IMAGE_INCLUDE_DIRS}
  ${XCB_KEYSYMS_INCLUDE_DIRS}
  ${XCB_XTEST_INCLUDE_DIRS}
  ${XCB_UTIL_INCLUDE_DIRS}
)

add_executable(zoom_bot
  src/main.cpp
  src/http_server.cpp
  src/zoom_client.cpp
  src/recorder.cpp
  src/meeting_url.cpp
  src/wav_writer.cpp
  src/mixer.cpp
  src/timeline.cpp
  src/vad.cpp
  src/util_sanitize.cpp
)

add_library(meetingsdk SHARED IMPORTED)
set_target_properties(meetingsdk PROPERTIES IMPORTED_LOCATION "${SDK_ROOT}/libmeetingsdk.so")

target_link_options(zoom_bot PRIVATE "-Wl,--no-as-needed")
target_link_options(zoom_bot PRIVATE "-Wl,-rpath,${SDK_ROOT}" "-Wl,-rpath,${SDK_ROOT}/qt_libs/Qt/lib")
target_link_libraries(zoom_bot PRIVATE
  meetingsdk
  Threads::Threads
  dl
  X11
  Xfixes
  ${DBUS_LIBRARIES}
  ${XCB_LIBRARIES}
  ${XCB_RANDR_LIBRARIES}
  ${XCB_SHM_LIBRARIES}
  ${XCB_XFIXES_LIBRARIES}
  ${XCB_SHAPE_LIBRARIES}
  ${XCB_IMAGE_LIBRARIES}
  ${XCB_KEYSYMS_LIBRARIES}
  ${XCB_XTEST_LIBRARIES}
  ${XCB_UTIL_LIBRARIES}
  GL
  gbm
  drm
)

install(TARGETS zoom_bot RUNTIME DESTINATION /usr/local/bin)
