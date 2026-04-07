cmake_minimum_required(VERSION 3.24)
project(SOIP LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/third_party)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------- Threads ----------
find_package(Threads REQUIRED)

# ---------- OpenGL ----------
find_package(OpenGL REQUIRED)

# ---------- FFTW ----------
find_library(FFTW3F_LIB fftw3f REQUIRED)
find_library(FFTW3F_THREADS_LIB fftw3f_threads REQUIRED)
find_path(FFTW3F_INCLUDE_DIR fftw3.h REQUIRED)

set(FFTW3F_LIBRARIES ${FFTW3F_THREADS_LIB} ${FFTW3F_LIB})
message(STATUS "Found FFTW3F: ${FFTW3F_LIB}")
message(STATUS "Found FFTW3F_THREADS: ${FFTW3F_THREADS_LIB}")

# ---------- SDL2 ----------
find_package(SDL2 CONFIG QUIET)

if(NOT SDL2_FOUND)
  message(STATUS "SDL2 not found, fetching...")
  fetchcontent_declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG release-2.28.5
  )
  fetchcontent_makeavailable(SDL2)
endif()

# ---------- GLEW ----------
find_package(GLEW QUIET)

if(NOT GLEW_FOUND)
  message(STATUS "GLEW not found, fetching...")
  fetchcontent_declare(
    glew
    GIT_REPOSITORY https://github.com/nigels-com/glew
    GIT_TAG glew-2.2.0
  )
  fetchcontent_makeavailable(glew)
endif()

# ---------- SoapySDR ----------
find_package(SoapySDR CONFIG QUIET)

if(NOT SoapySDR_FOUND)
  message(STATUS "SoapySDR not found, fetching...")
  fetchcontent_declare(
    SoapySDR
    GIT_REPOSITORY https://github.com/pothosware/SoapySDR
    GIT_TAG soapy-sdr-0.8.1
  )
  fetchcontent_makeavailable(SoapySDR)
endif()

# ---------- ImGui ----------
fetchcontent_declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG v1.92.6-docking
)

fetchcontent_makeavailable(imgui)

# ---------- spdlog ----------
fetchcontent_declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG v1.17.0
)

fetchcontent_makeavailable(spdlog)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui
  PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui
  PUBLIC
  SDL2::SDL2
  OpenGL::GL
)

# ---------- ImPlot ----------
fetchcontent_declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot
  GIT_TAG master
)

fetchcontent_makeavailable(implot)

if(TARGET SoapySDR::SoapySDR)
  set(SOAPY_TARGET SoapySDR::SoapySDR)
elseif(TARGET SoapySDR)
  set(SOAPY_TARGET SoapySDR)
else()
  message(FATAL_ERROR "SoapySDR target not found")
endif()

add_library(implot STATIC
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_demo.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp
)

target_include_directories(implot PUBLIC
  ${implot_SOURCE_DIR}
)

target_link_libraries(implot PUBLIC imgui)

# ---------- ZeroMQ (Core) ----------
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_CPACK OFF CACHE BOOL "" FORCE)

fetchcontent_declare(
  libzmq
  GIT_REPOSITORY https://github.com/zeromq/libzmq
  GIT_TAG v4.3.5
)
fetchcontent_makeavailable(libzmq)

# ---------- cppzmq (C++ Bindings) ----------
fetchcontent_declare(
  cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq
  GIT_TAG v4.10.0
)
fetchcontent_makeavailable(cppzmq)

# ---------- Core SDR library ----------
add_library(soip_core STATIC
  ${PROJECT_SOURCE_DIR}/src/ip/ip_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/ip/tun_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/ip/bit_utils.cpp

  ${PROJECT_SOURCE_DIR}/src/phy/phy_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/phy/sdr.cpp
  ${PROJECT_SOURCE_DIR}/src/phy/dsp.cpp
  ${PROJECT_SOURCE_DIR}/src/sockets.cpp
)

add_library(soip_gui STATIC
  ${PROJECT_SOURCE_DIR}/src/gui/gui_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/gui/phy_dev.cpp
  ${PROJECT_SOURCE_DIR}/src/gui/ip_dev.cpp
  ${PROJECT_SOURCE_DIR}/src/gui/app.cpp
)

target_include_directories(soip_core
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${FFTW3F_INCLUDE_DIR}
)
target_include_directories(soip_gui
  PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(soip_core
  PRIVATE
  ${SOAPY_TARGET}
  ${FFTW3F_THREADS_LIB}
  ${FFTW3F_LIB}
  PUBLIC
  Threads::Threads
  spdlog::spdlog
  cppzmq
)
target_compile_options(soip_core PRIVATE -mavx2 -ffast-math)

target_link_libraries(soip_gui
  PUBLIC
  soip_core
  imgui
  implot
  GLEW::GLEW
  SDL2::SDL2
  OpenGL::GL
)

# ---------- GUI executable ----------
add_executable(main
  src/main.cpp
)

add_executable(gui
  src/gui.cpp
)

target_link_libraries(main PRIVATE
  soip_core
)

target_link_libraries(gui PRIVATE
  soip_gui
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  foreach(target main gui)
    target_compile_options(${target} PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
      -g
    )
    target_link_options(${target} PRIVATE
      -fsanitize=address,undefined
    )
  endforeach()
endif()
