cmake_minimum_required(VERSION 3.22)

# ---- Version: from -DRTG_VERSION=X.Y.Z (release workflow) or VERSION file --
if(NOT DEFINED RTG_VERSION)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RTG_VERSION)
endif()
string(STRIP "${RTG_VERSION}" RTG_VERSION)
string(REGEX REPLACE "^v" "" RTG_VERSION "${RTG_VERSION}")

project(RiddimTrapGenerator VERSION ${RTG_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(RTG_BUILD_APP "Build the JUCE GUI application" ON)

set(RTG_GITHUB_OWNER "jathanalston-ship-it" CACHE STRING "GitHub owner for update checks")
set(RTG_GITHUB_REPO  "Riddim-Trap-Generator" CACHE STRING "GitHub repo for update checks")
set(RTG_UPDATE_ASSET "RiddimTrapGenerator-win64.exe" CACHE STRING "Release asset the updater downloads")

if(MSVC)
    # Static runtime → single-file portable exe.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8)
endif()

# ---- rtg_core: the entire engine, JUCE-free ---------------------------------
file(GLOB_RECURSE RTG_CORE_SOURCES CONFIGURE_DEPENDS
    engine/decision/src/*.cpp
    engine/arrangement/src/*.cpp
    engine/composition/src/*.cpp
    engine/sound_design/src/*.cpp
    engine/bass_designer/src/*.cpp
    engine/drum_generator/src/*.cpp
    engine/mix/src/*.cpp
    engine/master/src/*.cpp
    engine/generation/src/*.cpp
    library/src/*.cpp
    utils/src/*.cpp
)

add_library(rtg_core STATIC ${RTG_CORE_SOURCES})
target_include_directories(rtg_core PUBLIC
    utils/include
    engine/decision/include
    engine/composition/include
    engine/sound_design/include
    engine/mix/include
    engine/master/include
    engine/generation/include
    engine/render/include
    library/include
)
if(NOT MSVC)
    target_compile_options(rtg_core PRIVATE -O2)
endif()

# ---- rtg_cli: headless generator (CI smoke tests, local verification) -------
add_executable(rtg_cli tools/rtg_cli/main.cpp)
target_link_libraries(rtg_cli PRIVATE rtg_core)

# ---- GUI application ---------------------------------------------------------
if(RTG_BUILD_APP)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
        add_subdirectory(third_party/JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_gui_app(RiddimTrapGenerator
        PRODUCT_NAME "Riddim Trap Generator"
        COMPANY_NAME "RTG"
        VERSION ${RTG_VERSION}
    )

    file(GLOB_RECURSE RTG_APP_SOURCES CONFIGURE_DEPENDS
        app/src/*.cpp
        ui/src/*.cpp
    )
    target_sources(RiddimTrapGenerator PRIVATE ${RTG_APP_SOURCES})

    target_include_directories(RiddimTrapGenerator PRIVATE app/src ui/src)

    target_compile_definitions(RiddimTrapGenerator PRIVATE
        RTG_VERSION="${RTG_VERSION}"
        RTG_GITHUB_OWNER="${RTG_GITHUB_OWNER}"
        RTG_GITHUB_REPO="${RTG_GITHUB_REPO}"
        RTG_UPDATE_ASSET="${RTG_UPDATE_ASSET}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RiddimTrapGenerator,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RiddimTrapGenerator,JUCE_VERSION>"
    )

    target_link_libraries(RiddimTrapGenerator PRIVATE
        rtg_core
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
endif()
