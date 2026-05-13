cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(QuestForRome LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QUEST_FIRST_VERSION_MODE "Use original prototype behavior (no animation/combat/json map)" OFF)
option(QUEST_BUILD_EDITOR "Build wxWidgets content editor" ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(PNG REQUIRED)
find_package(nlohmann_json REQUIRED CONFIG)

add_executable(quest_for_rome
    src/Game.cpp
    src/MapLoader.cpp
    src/World.cpp
    src/main.cpp
)

target_link_libraries(quest_for_rome PRIVATE SDL3::SDL3)
target_link_libraries(quest_for_rome PRIVATE PNG::PNG)
target_link_libraries(quest_for_rome PRIVATE nlohmann_json::nlohmann_json)

if(QUEST_FIRST_VERSION_MODE)
    target_compile_definitions(quest_for_rome PRIVATE QUEST_FIRST_VERSION_MODE=1)
endif()

if (QUEST_BUILD_EDITOR)
    set(QUEST_WX_ROOT "C:/Tools/wxWidgets" CACHE PATH "Preferred wxWidgets root")
    if (EXISTS "${QUEST_WX_ROOT}")
        set(wxWidgets_ROOT_DIR "${QUEST_WX_ROOT}")
    endif()

    find_package(wxWidgets CONFIG QUIET)
    set(QUEST_USE_WX_CONFIG OFF)
    if (wxWidgets_FOUND)
        set(QUEST_USE_WX_CONFIG ON)
    else()
        find_package(wxWidgets REQUIRED COMPONENTS core base)
        include(${wxWidgets_USE_FILE})
    endif()

    add_executable(quest_for_rome_editor WIN32
        editor/EditorMain.cpp
        src/MapLoader.cpp
    )

    target_include_directories(quest_for_rome_editor PRIVATE src)
    target_link_libraries(quest_for_rome_editor PRIVATE SDL3::SDL3 nlohmann_json::nlohmann_json)

    if (QUEST_USE_WX_CONFIG)
        if (TARGET wx::core)
            target_link_libraries(quest_for_rome_editor PRIVATE wx::base wx::core)
        elseif (TARGET wx::wxWidgets)
            target_link_libraries(quest_for_rome_editor PRIVATE wx::wxWidgets)
        else()
            message(FATAL_ERROR "wxWidgets CONFIG found but known targets are missing.")
        endif()
    else()
        target_link_libraries(quest_for_rome_editor PRIVATE ${wxWidgets_LIBRARIES})
        target_include_directories(quest_for_rome_editor PRIVATE ${wxWidgets_INCLUDE_DIRS})
    endif()
endif()

if (MSVC)
    target_compile_options(quest_for_rome PRIVATE /W4 /permissive-)
else()
    target_compile_options(quest_for_rome PRIVATE -Wall -Wextra -Wpedantic)
endif()
