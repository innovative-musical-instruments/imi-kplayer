cmake_minimum_required(VERSION 3.22)
project(IMI_KPlayer VERSION 0.9.7)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

# Off by default so day-to-day local builds (build/) stay exactly as they've
# always been: ad-hoc "Sign to Run Locally", no hardened runtime. Turn this
# on (in a separate build directory, e.g. build-release/) to produce a
# Developer ID-signed, hardened-runtime build suitable for notarization -
# see scripts/build_release.sh.
option(KPLAYER_RELEASE_BUILD "Sign with Developer ID + enable hardened runtime, for notarized distribution" OFF)
set(KPLAYER_DEVELOPMENT_TEAM "B74YHJSPSY" CACHE STRING "Apple Developer Team ID used for release signing")

if(APPLE)
    add_subdirectory(/Users/amirvinci/SDKs/JUCE JUCE)
elseif(WIN32)
    add_subdirectory(C:/SDKs/JUCE JUCE)
endif()

# Binary-embedded assets (logos, About/splash art, app icons) so the app
# doesn't depend on files existing next to the binary at runtime.
juce_add_binary_data(KPlayerAssets SOURCES
    Assets/branding/imi-logo-white.png
    Assets/branding/tribal-tools-logo.png
    Assets/about/kplayer-256.png
    Assets/about/Azonix.otf
    Assets/fonts/SpaceGrotesk-Regular.ttf
    Assets/fonts/SpaceGrotesk-Bold.ttf
)

juce_add_gui_app(IMI_KPlayer
    PRODUCT_NAME "Kadabra K-Player"
    COMPANY_NAME "IMI"
    BUNDLE_ID "com.imi.kplayer"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kadabra K-Player needs microphone/audio input access to route live audio input into channels."
    # Without this, clicking "Bluetooth MIDI..." in Settings (JUCE's
    # BluetoothMidiDevicePairingDialogue) hard-crashes the app: macOS's TCC
    # framework aborts any process that touches Bluetooth with no
    # NSBluetoothAlwaysUsageDescription in its Info.plist
    # (__TCC_CRASHING_DUE_TO_PRIVACY_VIOLATION__ in the crash report), it
    # doesn't just deny access - same root category as the microphone
    # permission above, but a crash instead of silent denial.
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Kadabra K-Player uses Bluetooth to pair with Bluetooth MIDI controllers."
    HARDENED_RUNTIME_ENABLED ${KPLAYER_RELEASE_BUILD}
    HARDENED_RUNTIME_OPTIONS
        com.apple.security.cs.disable-library-validation
        com.apple.security.cs.allow-dyld-environment-variables
        com.apple.security.cs.allow-unsigned-executable-memory
        com.apple.security.device.audio-input
    # 1024px full-bleed master art (do not pre-mask for macOS - it applies
    # its own squircle clip + shadow) and a pre-scaled 256px version; JUCE
    # generates the .icns/.ico from these at build time. Plugin binary
    # icons (KSampler, built via HISE) are out of scope here.
    ICON_BIG "${CMAKE_SOURCE_DIR}/Assets/icons/kplayer-1024.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/Assets/icons/kplayer-256.png"
)

# disable-library-validation is what actually matters here: it's what lets
# a Developer ID-signed, hardened-runtime K-Player load third-party VST3/AU
# plugins signed by other developers/Team IDs at all - without it, macOS
# would refuse to load any plugin not signed by IMI's own team, which is
# obviously unworkable for a plugin host. The other two options cover
# plugins that rely on JIT-compiled DSP/scripting or dyld environment
# tricks (common in synths/samplers) - harmless to enable even if unused.
if(KPLAYER_RELEASE_BUILD)
    set_target_properties(IMI_KPlayer PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${KPLAYER_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--timestamp"
        # Xcode auto-injects com.apple.security.get-task-allow=true into the
        # final signed entitlements on any plain `build` action (regardless
        # of Release/Debug config) unless this is disabled - the notary
        # service rejects any submission with that entitlement present.
        XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "NO"
        # Override explicitly with the literal Xcode expects - the
        # HARDENED_RUNTIME_ENABLED value juce_add_gui_app() passed through
        # above is CMake's ON/OFF boolean spelling, which Xcode's build
        # system doesn't recognize as true, so hardened runtime silently
        # never actually got enabled in the codesign flags without this.
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
    )
endif()

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
    Source/GlobalSectionComponent.cpp
    Source/GlobalSectionComponent.h
    Source/GearButtonLookAndFeel.h
    Source/PeakMeterComponent.cpp
    Source/PeakMeterComponent.h
    Source/PluginEditorWindow.h
    Source/KPlayerAudioPlayHead.h
    Source/TempoSyncComponent.cpp
    Source/TempoSyncComponent.h
    Source/MidiClockTempoDetector.h
    Source/RecordingManager.cpp
    Source/RecordingManager.h
    Source/MidiTakePlayer.cpp
    Source/MidiTakePlayer.h
    Source/AudioTakePlayer.cpp
    Source/AudioTakePlayer.h
    Source/SessionTransport.h
    Source/TransportButtonLookAndFeel.h
    Source/ConsoleFaderLookAndFeel.h
    Source/LoadingOverlayComponent.h
    Source/SessionIO.cpp
    Source/SessionIO.h
    Source/SessionMigrator.cpp
    Source/SessionMigrator.h
    Source/SessionFormat.cpp
    Source/SessionFormat.h
    Source/BrandingStripComponent.h
    Source/AboutScreenComponent.cpp
    Source/AboutScreenComponent.h
)

target_compile_definitions(IMI_KPlayer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    # Decode-only MP3 support for Import Audio to Track (Increment E, see
    # docs/kplayer-take-recording-playback-spec.md section 9) - off by
    # default in JUCE, unlike WAV/AIFF/FLAC/OggVorbis.
    JUCE_USE_MP3AUDIOFORMAT=1
    # Off by default in JUCE (0). Needed for MessageManager::
    # runDispatchLoopUntil() - see MainComponent::showWarmingUpOverlay(),
    # the one place this app actually needs a real, synchronous pump of the
    # event loop (forcing the loading overlay's startup message to actually
    # paint before a long blocking call). Every other blocking-UI pattern in
    # this codebase deliberately stays async (AlertWindow::showAsync,
    # FileChooser::launchAsync, etc.) - this narrow opt-in doesn't change
    # that, it only unlocks the one JUCE API that needed it.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:IMI_KPlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:IMI_KPlayer,JUCE_VERSION>"
)

target_link_libraries(IMI_KPlayer PRIVATE
    KPlayerAssets
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
