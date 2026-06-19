cmake_minimum_required(VERSION 3.24)
project(SOIP LANGUAGES CXX)

find_program(CCACHE_PROGRAM ccache)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/third_party)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
  set(DEBUG_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -g)
endif()

# ---------- Threads ----------
find_package(Threads REQUIRED)

# ---------- OpenGL ----------
find_package(OpenGL REQUIRED)

# ---------- FFTW ----------
find_library(FFTW3F_LIB fftw3f REQUIRED)
find_path(FFTW3F_INCLUDE_DIR fftw3.h REQUIRED)

set(FFTW3F_LIBRARIES ${FFTW3F_LIB})
message(STATUS "Found FFTW3F: ${FFTW3F_LIB}")

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

# ---------- cxxopts ----------
fetchcontent_declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts
  GIT_TAG v3.3.1
)
fetchcontent_makeavailable(cxxopts)

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
target_compile_options(implot PRIVATE -Wno-deprecated-enum-enum-conversion)

# ---------- ZeroMQ (Core) ----------
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_CPACK OFF CACHE BOOL "" FORCE)
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)

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

if(CCACHE_PROGRAM)
  message(STATUS "ccache will be used ONLY for target libraries: imgui, implot, zmq")

  set(TARGETS_TO_CACHE
    imgui
    implot
    libzmq
    libzmq-static
    cppzmq
  )

  foreach(target IN LISTS TARGETS_TO_CACHE)
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES
        C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}"
        CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}"
      )
    endif()
  endforeach()
else()
  message(WARNING "ccache not found - libraries will build without cache")
endif()


# ---------- Core SDR OPTIONS ----------
option(HAS_DYNAMIC_ENUMERATE "Build with dynamic SDR enumeration support" OFF)

if(HAS_DYNAMIC_ENUMERATE)
  add_compile_definitions(HAS_DYNAMIC_ENUMERATE)
endif()

# ---------- Core SDR library ----------
add_library(soip_core STATIC
  ${PROJECT_SOURCE_DIR}/src/ip/ip_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/ip/tun_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/ip/crc.cpp
  ${PROJECT_SOURCE_DIR}/src/ip/fec_codec.cpp
  ${PROJECT_SOURCE_DIR}/src/ip/ip_nat.cpp

  ${PROJECT_SOURCE_DIR}/src/phy/phy_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/phy/sdr.cpp
  ${PROJECT_SOURCE_DIR}/src/phy/dsp.cpp

  ${PROJECT_SOURCE_DIR}/src/sockets.cpp
  ${PROJECT_SOURCE_DIR}/src/logger.cpp
  ${PROJECT_SOURCE_DIR}/src/cli.cpp
  ${PROJECT_SOURCE_DIR}/src/config.cpp
  ${PROJECT_SOURCE_DIR}/src/bridges.cpp
)

if(DEBUG_FLAGS)
  target_compile_options(soip_core PRIVATE ${DEBUG_FLAGS})
  target_compile_options(soip_core PRIVATE -mavx2)
  target_link_options(soip_core PRIVATE ${DEBUG_FLAGS})
else()
  target_compile_options(soip_core PRIVATE -mavx2 -ffast-math)
endif()

add_library(soip_gui STATIC
  ${PROJECT_SOURCE_DIR}/src/gui/gui_layer.cpp
  ${PROJECT_SOURCE_DIR}/src/gui/phy_dev.cpp
  ${PROJECT_SOURCE_DIR}/src/gui/ip_dev.cpp
  ${PROJECT_SOURCE_DIR}/src/gui/app.cpp
)

if(DEBUG_FLAGS)
  target_compile_options(soip_gui PRIVATE ${DEBUG_FLAGS})
  target_link_options(soip_gui PRIVATE ${DEBUG_FLAGS})
endif()

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
  ${FFTW3F_LIB}
  PUBLIC
  Threads::Threads
  spdlog::spdlog
  cxxopts::cxxopts
  cppzmq
)

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
add_executable(soip
  src/main.cpp
)

add_executable(gui
  src/gui.cpp
)

target_link_libraries(soip PRIVATE
  soip_core
)

target_link_libraries(gui PRIVATE
  soip_gui
)

if(DEBUG_FLAGS)
  target_compile_options(soip PRIVATE ${DEBUG_FLAGS})
  target_link_options(soip PRIVATE ${DEBUG_FLAGS})

  target_compile_options(gui PRIVATE ${DEBUG_FLAGS})
  target_link_options(gui PRIVATE ${DEBUG_FLAGS})
endif()
