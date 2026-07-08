cmake_minimum_required(VERSION 3.22)
project(IMI_KPlayer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

if(APPLE)
    add_subdirectory(/Users/amirvinci/SDKs/JUCE JUCE)
elseif(WIN32)
    add_subdirectory(C:/SDKs/JUCE JUCE)
endif()

juce_add_gui_app(IMI_KPlayer
    PRODUCT_NAME "Kadabra K-Player"
    COMPANY_NAME "IMI"
    BUNDLE_ID "com.imi.kplayer"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kadabra K-Player needs microphone/audio input access to route live audio input into channels."
)

target_sources(IMI_KPlayer PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/SettingsComponent.cpp
    Source/SettingsComponent.h
    Source/PluginManager.cpp
    Source/PluginManager.h
    Source/PluginBrowserComponent.cpp
    Source/PluginBrowserComponent.h
    Source/ChannelProcessor.cpp
    Source/ChannelProcessor.h
    Source/ChannelComponent.cpp
    Source/ChannelComponent.h
    Source/MasterChainProcessor.cpp
    Source/MasterChainProcessor.h
    Source/MasterChainComponent.cpp
    Source/MasterChainComponent.h
    Source/PeakMeterComponent.cpp
    Source/PeakMeterComponent.h
    Source/PluginEditorWindow.h
    Source/KPlayerAudioPlayHead.h
    Source/ConsoleFaderLookAndFeel.h
    Source/LoadingOverlayComponent.h
    Source/SessionIO.cpp
    Source/SessionIO.h
    Source/SessionMigrator.cpp
    Source/SessionMigrator.h
    Source/SessionFormat.cpp
    Source/SessionFormat.h
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

# Regression tests for the session format versioning/migration system (see
# docs/K-Player_Session_Format_Versioning_Spec_Increment0.md §6). Kept as a
# separate juce_core-only console app so it builds fast and doesn't need
# audio devices, plugin hosting, or a GUI to run.
juce_add_console_app(IMI_KPlayer_Tests
    PRODUCT_NAME "IMI_KPlayer_Tests"
)

target_sources(IMI_KPlayer_Tests PRIVATE
    Tests/TestMain.cpp
    Tests/SessionMigratorTests.cpp
    Tests/SessionFormatTests.cpp
    Source/SessionMigrator.cpp
    Source/SessionMigrator.h
    Source/SessionFormat.cpp
    Source/SessionFormat.h
)

target_include_directories(IMI_KPlayer_Tests PRIVATE Source)

target_compile_definitions(IMI_KPlayer_Tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    KPLAYER_TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/test/fixtures/session-formats"
)

target_link_libraries(IMI_KPlayer_Tests PRIVATE
    juce::juce_core
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME SessionFormatTests COMMAND IMI_KPlayer_Tests)
