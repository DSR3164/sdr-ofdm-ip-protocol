cmake_minimum_required(VERSION 3.24)
project(SOIP VERSION 0.1.0 LANGUAGES CXX)

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# FetchContent
include(FetchContent)
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/third_party)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FFTW3F REQUIRED fftw3f)
find_library(FFTW3F_THREADS_LIB fftw3f_threads REQUIRED)

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
  message(STATUS "SDL2 not found in system, fetching...")
  FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL GIT_TAG release-2.28.5)
  FetchContent_MakeAvailable(SDL2)
endif()

find_package(GLEW QUIET)
if(NOT GLEW_FOUND)
  message(STATUS "GLEW not found in system, fetching...")
  FetchContent_Declare(glew GIT_REPOSITORY https://github.com/nigels-com/glew GIT_TAG glew-2.2.0)
  FetchContent_MakeAvailable(glew)
endif()

find_package(SoapySDR CONFIG QUIET)
if(NOT SoapySDR_FOUND)
  message(STATUS "SoapySDR not found in system, fetching...")
  FetchContent_Declare(SoapySDR GIT_REPOSITORY https://github.com/pothosware/SoapySDR GIT_TAG soapy-sdr-0.8.1)
  FetchContent_MakeAvailable(SoapySDR)
endif()

pkg_check_modules(ZMQ REQUIRED libzmq>=4.3.0)
find_package(cppzmq REQUIRED)

FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui GIT_TAG v1.92.6-docking)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog GIT_TAG v1.17.0)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(cxxopts GIT_REPOSITORY https://github.com/jarro2783/cxxopts GIT_TAG v3.3.1)
FetchContent_MakeAvailable(cxxopts)

FetchContent_Declare(implot GIT_REPOSITORY https://github.com/epezent/implot GIT_TAG master)
FetchContent_MakeAvailable(implot)

FetchContent_Declare(implot3d GIT_REPOSITORY https://github.com/brenocq/implot3d GIT_TAG main)
FetchContent_MakeAvailable(implot3d)

# ImGui
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL GLEW::GLEW)

# ImPlot / ImPlot3D
add_library(implot STATIC ${implot_SOURCE_DIR}/implot.cpp ${implot_SOURCE_DIR}/implot_demo.cpp ${implot_SOURCE_DIR}/implot_items.cpp)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

add_library(implot3d STATIC ${implot3d_SOURCE_DIR}/implot3d.cpp ${implot3d_SOURCE_DIR}/implot3d_demo.cpp ${implot3d_SOURCE_DIR}/implot3d_items.cpp ${implot3d_SOURCE_DIR}/implot3d_meshes.cpp)
target_include_directories(implot3d PUBLIC ${implot3d_SOURCE_DIR})
target_link_libraries(implot3d PUBLIC imgui)

# SoapySDR target fix
if(TARGET SoapySDR::SoapySDR)
  set(SOAPY_TARGET SoapySDR::SoapySDR)
elseif(TARGET SoapySDR)
  set(SOAPY_TARGET SoapySDR)
else()
  message(FATAL_ERROR "SoapySDR target not found")
endif()

# Core library
add_library(soip_core STATIC
    src/ip/ip_layer.cpp src/ip/tun_layer.cpp src/ip/crc.cpp src/ip/fec_codec.cpp src/ip/ip_nat.cpp
    src/phy/phy_layer.cpp src/phy/sdr.cpp src/phy/dsp.cpp
    src/sockets.cpp src/logger.cpp src/cli.cpp src/config.cpp
)
target_include_directories(soip_core PUBLIC ${PROJECT_SOURCE_DIR}/include PRIVATE ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(soip_core
    PRIVATE ${SOAPY_TARGET} ${FFTW3F_THREADS_LIB} ${FFTW3F_LIBRARIES}
    PUBLIC Threads::Threads spdlog::spdlog cxxopts::cxxopts cppzmq
)
target_compile_options(soip_core PRIVATE -mavx2 -ffast-math)

# GUI library
add_library(soip_gui STATIC src/gui/gui_layer.cpp src/gui/phy_dev.cpp src/gui/ip_dev.cpp src/gui/app.cpp)
target_include_directories(soip_gui PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(soip_gui PUBLIC soip_core imgui implot implot3d GLEW::GLEW SDL2::SDL2 OpenGL::GL)

# Executables
add_executable(soip src/main.cpp)
target_link_libraries(soip PRIVATE soip_core)

add_executable(gui src/gui.cpp)
target_link_libraries(gui PRIVATE soip_gui)

# Debug sanitizers
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  foreach(target soip gui)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

# Install and systemd
include(GNUInstallDirs)

if(NOT DEFINED CMAKE_INSTALL_SYSTEMDUNITDIR)
  set(CMAKE_INSTALL_SYSTEMDUNITDIR "${CMAKE_INSTALL_LIBDIR}/systemd/system" CACHE PATH "Systemd unit files")
endif()

set(BIN_PATH "${CMAKE_INSTALL_FULL_BINDIR}/soip")
set(ARGS "--config ${CMAKE_INSTALL_FULL_SYSCONFDIR}/soip/sdr.conf")
set(INSTALL_DIR "/var/lib/soip")
set(SERVICE_USER "soip")

configure_file(
    ${CMAKE_SOURCE_DIR}/config/soip.service.in
    ${CMAKE_BINARY_DIR}/soip.service
    @ONLY
)

install(TARGETS soip gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_BINARY_DIR}/soip.service DESTINATION ${CMAKE_INSTALL_SYSTEMDUNITDIR})
install(FILES ${CMAKE_SOURCE_DIR}/config/sdr.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/soip)

# Cpack
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "soip")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "SOIP Team <soip@soip.com>")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/packaging/postinst")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
