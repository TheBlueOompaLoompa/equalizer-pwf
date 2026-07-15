cmake_minimum_required(VERSION 3.20)
project(equalizer-pwf)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -O2")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/imgui)
set(IMPLOT_DIR ${PROJECT_SOURCE_DIR}/implot)
set(DSP_DIR ${PROJECT_SOURCE_DIR}/dsp)

file(COPY org.bloompa.EqualizerPWF.yml DESTINATION .)
file(COPY equalizer-pwf.desktop DESTINATION .)
file(COPY equalizer-pwf.svg DESTINATION .)

# Includes
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${IMGUI_DIR})
include_directories(${IMGUI_DIR}/backends)
include_directories(${IMPLOT_DIR})
include_directories(${DSP_DIR})

# Glob Sources
file(GLOB SOURCES src/*.cpp)

file(GLOB IMGUI_SOURCES ${IMGUI_DIR}/*.cpp)
set(IMGUI_SOURCES ${IMGUI_SOURCES} ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp)
file(GLOB IMPLOT_SOURCES ${IMPLOT_DIR}/*.cpp)

# Packages
set(SDL_STATIC ON)
set(SDL_SHARED OFF)
add_subdirectory(SDL EXCLUDE_FROM_ALL)

find_package(PkgConfig QUIET REQUIRED)
pkg_check_modules(PipeWire REQUIRED IMPORTED_TARGET libpipewire-0.3)

add_executable(equalizer-pwf ${SOURCES} ${IMGUI_SOURCES} ${IMPLOT_SOURCES})

target_link_libraries(equalizer-pwf PRIVATE PkgConfig::PipeWire)
target_link_libraries(equalizer-pwf PRIVATE SDL3::SDL3)

target_include_directories(equalizer-pwf PUBLIC ${PROJECT_SOURCE_DIR}/inc)

install(
  TARGETS equalizer-pwf
)
