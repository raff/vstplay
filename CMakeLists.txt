cmake_minimum_required(VERSION 3.22)
project(VstPlay VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(VstPlay PRODUCT_NAME "VstPlay")

target_sources(VstPlay PRIVATE Source/Main.cpp)

target_compile_definitions(VstPlay PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VstPlay,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${CMAKE_PROJECT_VERSION}"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(VstPlay PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
