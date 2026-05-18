cmake_minimum_required(VERSION 3.16)
project(twain VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TWAIN_DEBUG "Enable twain debug instrumentation (logs to /tmp/twain-PID.log)" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(twain
    src/main.cpp
    src/MainWindow.cpp
    src/DiffView.cpp
    src/DiffOverview.cpp
    src/DiffPane.cpp
    src/Diff.cpp
    src/DebugLog.cpp
    src/TreeCompareView.cpp
    src/TreeCompare.cpp
    src/TreeCompareModel.cpp
    resources/icons.qrc
)

target_link_libraries(twain PRIVATE Qt6::Widgets)

if(TWAIN_DEBUG)
    target_compile_definitions(twain PRIVATE TWAIN_DEBUG=1)
    set_target_properties(twain PROPERTIES OUTPUT_NAME twain-debug)
endif()
