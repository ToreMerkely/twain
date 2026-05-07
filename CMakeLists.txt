cmake_minimum_required(VERSION 3.16)
project(twain VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(twain
    src/main.cpp
    src/MainWindow.cpp
    src/DiffView.cpp
    src/DiffOverview.cpp
    src/DiffPane.cpp
    src/Diff.cpp
    src/HighlighterFactory.cpp
    src/TreeCompareView.cpp
    src/TreeCompare.cpp
    src/TreeCompareModel.cpp
    src/CppHighlighter.cpp
    src/PythonHighlighter.cpp
    src/JsonHighlighter.cpp
    src/YamlHighlighter.cpp
    src/ShellHighlighter.cpp
    src/MakefileHighlighter.cpp
    src/MarkdownHighlighter.cpp
    resources/icons.qrc
)

target_link_libraries(twain PRIVATE Qt6::Widgets)
