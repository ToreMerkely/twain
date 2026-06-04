cmake_minimum_required(VERSION 3.16)
project(twain VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TWAIN_DEBUG "Enable twain debug instrumentation (logs to /tmp/twain-PID.log)" OFF)
option(TWAIN_TESTS "Build twain unit tests" OFF)
option(TWAIN_COVERAGE "Instrument the build with gcov coverage flags" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(twain
    src/main.cpp
    src/MainWindow.cpp
    src/DiffView.cpp
    src/DiffOverview.cpp
    src/DiffPane.cpp
    src/Diff.cpp
    src/DebugLog.cpp
    src/ImageDiffView.cpp
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

# When coverage is on, instrument the main twain target too so every
# src/*.cpp produces a .gcno file. Files that no test actually exercises
# then show up at 0% rather than disappearing from the report.
if(TWAIN_COVERAGE)
    target_compile_options(twain PRIVATE --coverage -O0 -g)
    target_link_options(twain PRIVATE --coverage)
endif()

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

    # TreeCompareModel pulls in QtWidgets (QFileIconProvider) and needs a
    # QApplication, so it can't use the QtCore-only twain_add_test helper.
    add_executable(test_treecomparemodel
        tests/test_treecomparemodel.cpp
        src/TreeCompareModel.cpp)
    target_include_directories(test_treecomparemodel PRIVATE src)
    target_link_libraries(test_treecomparemodel PRIVATE Qt6::Widgets Qt6::Test)
    if(TWAIN_COVERAGE)
        target_compile_options(test_treecomparemodel PRIVATE --coverage -O0 -g)
        target_link_options(test_treecomparemodel PRIVATE --coverage)
    endif()
    add_test(NAME test_treecomparemodel COMMAND test_treecomparemodel)
    # Run headless under the offscreen platform plugin.
    set_tests_properties(test_treecomparemodel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Widget-level tests: full QtWidgets link, offscreen platform. The tests
    # provide their own main() (QSettings is redirected before QApplication).
    function(twain_add_widget_test NAME)
        # ARGN: the src/ files the widget under test pulls in.
        add_executable(${NAME} tests/${NAME}.cpp ${ARGN})
        target_include_directories(${NAME} PRIVATE src)
        target_link_libraries(${NAME} PRIVATE Qt6::Widgets Qt6::Test)
        if(TWAIN_COVERAGE)
            target_compile_options(${NAME} PRIVATE --coverage -O0 -g)
            target_link_options(${NAME} PRIVATE --coverage)
        endif()
        add_test(NAME ${NAME} COMMAND ${NAME})
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endfunction()

    # DiffView/DiffPane reach TreeCompareView through the app-wide font
    # broadcast, so the whole view layer links together.
    set(TWAIN_VIEW_SOURCES
        src/DiffView.cpp
        src/DiffPane.cpp
        src/DiffOverview.cpp
        src/Diff.cpp
        src/DebugLog.cpp
        src/TreeCompareView.cpp
        src/TreeCompare.cpp
        src/TreeCompareModel.cpp)
    twain_add_widget_test(test_diffpane ${TWAIN_VIEW_SOURCES})
    twain_add_widget_test(test_diffview ${TWAIN_VIEW_SOURCES})
    twain_add_widget_test(test_treecompareview ${TWAIN_VIEW_SOURCES})

    # ImageDiffView and DiffOverview have no twain-internal dependencies.
    twain_add_widget_test(test_imagediffview src/ImageDiffView.cpp)
    twain_add_widget_test(test_diffoverview src/DiffOverview.cpp)

    # MainWindow ties the whole view layer together.
    twain_add_widget_test(test_mainwindow
        src/MainWindow.cpp
        src/ImageDiffView.cpp
        ${TWAIN_VIEW_SOURCES})
endif()
