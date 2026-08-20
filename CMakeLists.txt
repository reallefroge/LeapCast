cmake_minimum_required(VERSION 3.24)
project(MultiChatStudio VERSION 19.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets WebEngineWidgets)

qt_add_executable(MultiChatStudio WIN32
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
    resources/resources.qrc
)

target_link_libraries(MultiChatStudio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::WebEngineWidgets
)

target_compile_definitions(MultiChatStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

install(TARGETS MultiChatStudio RUNTIME DESTINATION .)
