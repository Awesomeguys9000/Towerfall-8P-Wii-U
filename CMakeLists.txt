cmake_minimum_required(VERSION 3.16)
project(TowerFallPort CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

file(GLOB GEN_SOURCES ${CMAKE_SOURCE_DIR}/gen/src/*.cpp)
file(GLOB_RECURSE RT_SOURCES ${CMAKE_SOURCE_DIR}/runtime/sys/*.cpp ${CMAKE_SOURCE_DIR}/runtime/xna/*.cpp ${CMAKE_SOURCE_DIR}/runtime/native/*.cpp)

if(WIIU)
  set(PLATFORM_SOURCES ${CMAKE_SOURCE_DIR}/runtime/platform/platform_wiiu.cpp)
else()
  set(PLATFORM_SOURCES ${CMAKE_SOURCE_DIR}/runtime/platform/platform_desktop.cpp)
endif()

add_executable(towerfall ${GEN_SOURCES} ${RT_SOURCES} ${PLATFORM_SOURCES} ${CMAKE_SOURCE_DIR}/runtime/main.cpp)
target_include_directories(towerfall PRIVATE ${CMAKE_SOURCE_DIR}/runtime ${CMAKE_SOURCE_DIR}/runtime/third_party ${CMAKE_SOURCE_DIR}/gen)
target_precompile_headers(towerfall PRIVATE ${CMAKE_SOURCE_DIR}/gen/tf_all.h)
set_source_files_properties(${RT_SOURCES} ${PLATFORM_SOURCES} ${CMAKE_SOURCE_DIR}/runtime/main.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_target_properties(towerfall PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 24)
set_source_files_properties(${RT_SOURCES} ${PLATFORM_SOURCES} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(towerfall PRIVATE -Wno-everything -fno-strict-aliasing)
else()
  target_compile_options(towerfall PRIVATE -w -fno-strict-aliasing)
endif()

if(WIIU)
  find_package(SDL2 REQUIRED)
  target_compile_definitions(towerfall PRIVATE TF_WIIU=1)
  target_compile_options(towerfall PRIVATE -O2 -ffunction-sections -fdata-sections -fno-jump-tables)
  target_link_libraries(towerfall PRIVATE SDL2::SDL2 SDL2::SDL2main)
  wut_create_rpx(towerfall)
  # elf2rpl writes a 64 KB main-thread stack; give the game 8 MB (it runs on the main thread)
  # TF_PYTHON_CMD: optional command list that runs a Python script (the builder app passes itself)
  if(TF_PYTHON_CMD)
    set(TF_PY ${TF_PYTHON_CMD})
  else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(TF_PY ${Python3_EXECUTABLE})
  endif()
  add_custom_command(TARGET towerfall POST_BUILD
    COMMAND ${TF_PY} ${CMAKE_SOURCE_DIR}/wiiu/patch_rpx_stack.py ${CMAKE_CURRENT_BINARY_DIR}/towerfall.rpx 0x800000
    COMMENT "Patching RPX main-thread stack size")
  wut_create_wuhb(towerfall
    NAME "TowerFall 8P"
    SHORTNAME "TowerFall"
    AUTHOR "Matt Makes Games (port)"
    ICON "${CMAKE_SOURCE_DIR}/wiiu/icon.png"
    TVSPLASH "${CMAKE_SOURCE_DIR}/wiiu/tv-splash.png"
    DRCSPLASH "${CMAKE_SOURCE_DIR}/wiiu/drc-splash.png")
else()
  find_path(SDL2_INCLUDE SDL2/SDL.h PATHS /opt/homebrew/include /usr/local/include)
  find_library(SDL2_LIB SDL2 PATHS /opt/homebrew/lib /usr/local/lib)
  target_include_directories(towerfall PRIVATE ${SDL2_INCLUDE} ${SDL2_INCLUDE}/SDL2)
  target_link_libraries(towerfall PRIVATE ${SDL2_LIB})
endif()
