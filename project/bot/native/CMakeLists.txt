cmake_minimum_required(VERSION 3.16)
project(zoom_bot_recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(zoom_bot_recorder main.cpp)

target_include_directories(zoom_bot_recorder PRIVATE /app/zoom_sdk/include)

file(GLOB ZOOM_SDK_LIBS "/app/zoom_sdk/lib/*.so" "/app/zoom_sdk/lib/*.a")
if(ZOOM_SDK_LIBS)
  target_link_libraries(zoom_bot_recorder PRIVATE ${ZOOM_SDK_LIBS})
endif()

set_target_properties(zoom_bot_recorder PROPERTIES
  BUILD_RPATH "/app/zoom_sdk/lib"
  INSTALL_RPATH "/app/zoom_sdk/lib"
)
