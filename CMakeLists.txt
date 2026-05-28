cmake_minimum_required(VERSION 3.16)
project(twain VERSION 1.0.0 LANGUAGES CXX)

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

option(TWAIN_TESTS "Build twain unit tests" OFF)
option(TWAIN_COVERAGE "Instrument the test build with gcov coverage flags" OFF)
if(TWAIN_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    function(twain_add_test NAME)
        # ARGN: extra source files (e.g. the src/ file under test).
        add_executable(${NAME} tests/${NAME}.cpp ${ARGN})
        target_include_directories(${NAME} PRIVATE src)
        target_link_libraries(${NAME} PRIVATE Qt6::Core Qt6::Test)
        if(TWAIN_COVERAGE)
            target_compile_options(${NAME} PRIVATE --coverage -O0 -g)
            target_link_options(${NAME} PRIVATE --coverage)
        endif()
        add_test(NAME ${NAME} COMMAND ${NAME})
    endfunction()
    twain_add_test(test_diff         src/Diff.cpp)
    twain_add_test(test_treecompare  src/TreeCompare.cpp)
endif()
