cmake_minimum_required(VERSION 3.16)
project(zoom_bot_recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED SDK_ROOT)
  set(SDK_ROOT "/opt/sdk")
endif()
message(STATUS "SDK_ROOT=${SDK_ROOT}")

include_directories(
  ${SDK_ROOT}/h
  ${CMAKE_CURRENT_SOURCE_DIR}/src
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

target_link_libraries(zoom_bot PRIVATE pthread dl)

install(TARGETS zoom_bot RUNTIME DESTINATION /usr/local/bin)
