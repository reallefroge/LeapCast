cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LEAPCAST_VERSION)
string(STRIP "${LEAPCAST_VERSION}" LEAPCAST_VERSION)
if(DEFINED LEAPCAST_VERSION_OVERRIDE)
    set(LEAPCAST_VERSION "${LEAPCAST_VERSION_OVERRIDE}")
endif()
if(NOT LEAPCAST_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version such as 19.0.3")
endif()

project(LeapcastStudio VERSION ${LEAPCAST_VERSION} LANGUAGES CXX)

set(LEAPCAST_TWITCH_CLIENT_ID "" CACHE STRING
    "Public Twitch application Client ID used by the built-in device authorization flow")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets WebEngineWidgets)

configure_file(
    cmake/BuildInfo.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.hpp"
    @ONLY
)

qt_add_executable(LeapcastStudio WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.hpp
    src/Core.cpp
    src/Core.hpp
    src/LiveServices.cpp
    src/LiveServices.hpp
    src/PlatformServices.cpp
    src/PlatformServices.hpp
    src/AppController.cpp
    src/AppController.hpp
    src/Overlay.cpp
    src/Overlay.hpp
    src/UpdateService.cpp
    src/UpdateService.hpp
    resources/resources.qrc
)

if(WIN32)
    target_sources(LeapcastStudio PRIVATE resources/LeapcastStudio.rc)
endif()

target_include_directories(LeapcastStudio PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(LeapcastStudio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::WebEngineWidgets
)

target_compile_definitions(LeapcastStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

install(TARGETS LeapcastStudio RUNTIME DESTINATION .)
