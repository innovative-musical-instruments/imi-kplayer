# Fails the build if a linked Windows executable's FILEVERSION does not
# match the version it was supposed to be built as.
#
# Run in CMake script mode as a POST_BUILD step from the top-level
# CMakeLists.txt:
#   cmake -DEXE_PATH=<...> -DEXPECTED_VERSION=<x.y.z> -P cmake/AssertExeVersion.cmake
#
# Why this exists: JUCE bakes FILEVERSION / PRODUCTVERSION into a version
# resource generated at CONFIGURE time from project(... VERSION ...). A
# targeted `cmake --build --target IMI_KPlayer` with the Visual Studio
# generator skips ZERO_CHECK, so a bare project() version bump can leave
# that resource - and the linked .exe - on the previous version while
# JuceLibraryCode/Defs.txt and the app's own JUCE_APPLICATION_VERSION_STRING
# are already current. That shipped a 0.9.7-stamped "0.9.8" installer
# binary once (2026-08-28). This turns that state into a hard build
# failure instead of a silent mislabel.

if(NOT DEFINED EXE_PATH OR NOT DEFINED EXPECTED_VERSION)
    message(FATAL_ERROR "AssertExeVersion: EXE_PATH and EXPECTED_VERSION are both required")
endif()

if(NOT EXISTS "${EXE_PATH}")
    message(FATAL_ERROR "AssertExeVersion: no file at '${EXE_PATH}'")
endif()

# FileMajorPart/FileMinorPart/FileBuildPart are plain ints on every
# PowerShell (Windows PowerShell 5.1's .NET Framework included), unlike
# FileVersionRaw which is .NET 5+ only. Emit them one per line.
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
            "$v=[Diagnostics.FileVersionInfo]::GetVersionInfo('${EXE_PATH}'); $v.FileMajorPart; $v.FileMinorPart; $v.FileBuildPart"
    OUTPUT_VARIABLE _parts
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _probe_rc
    ERROR_VARIABLE  _probe_err
)
if(NOT _probe_rc EQUAL 0)
    message(FATAL_ERROR "AssertExeVersion: could not read a version from '${EXE_PATH}'\n${_probe_err}")
endif()

# "0\r\n9\r\n8" -> "0.9.8"
string(REGEX REPLACE "[ \t\r\n]+" "." _built_version "${_parts}")

if(NOT _built_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "AssertExeVersion: unparseable version '${_built_version}' from '${EXE_PATH}'")
endif()

if(NOT _built_version STREQUAL EXPECTED_VERSION)
    message(FATAL_ERROR
        "\n"
        "  Version mismatch in the linked executable\n"
        "    ${EXE_PATH}\n"
        "    built as        : ${_built_version}\n"
        "    project(VERSION) : ${EXPECTED_VERSION}\n"
        "\n"
        "  JUCE's generated version resource is stale. Reconfigure from a\n"
        "  clean state so it is regenerated from the current project() version:\n"
        "    cmake --fresh -B build -G \"<your Visual Studio generator>\"\n"
        "  or just run scripts/build_release_windows.ps1, which does that.\n")
endif()

message(STATUS "AssertExeVersion: ${EXE_PATH} is ${_built_version} (matches project VERSION)")
