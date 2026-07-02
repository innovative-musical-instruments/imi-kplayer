cmake_minimum_required(VERSION 3.22)
project(IMI_KPlayer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

add_subdirectory(/Users/amirvinci/SDKs/JUCE JUCE)

juce_add_gui_app(IMI_KPlayer
    PRODUCT_NAME "Kadabra K-Player"
    COMPANY_NAME "IMI"
    BUNDLE_ID "com.imi.kplayer"
)

target_sources(IMI_KPlayer PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/PreferencesComponent.cpp
    Source/PreferencesComponent.h
    Source/PluginManager.cpp
    Source/PluginManager.h
    Source/PluginBrowserComponent.cpp
    Source/PluginBrowserComponent.h
    Source/ChannelProcessor.cpp
    Source/ChannelProcessor.h
    Source/ChannelComponent.cpp
    Source/ChannelComponent.h
)

target_compile_definitions(IMI_KPlayer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:IMI_KPlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:IMI_KPlayer,JUCE_VERSION>"
)

target_link_libraries(IMI_KPlayer PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
