cmake_minimum_required(VERSION 3.16)
project(HashMusic
        VERSION 1.0.0
        DESCRIPTION "Turns any text or file into a melody chosen by a from-scratch SHA-256"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-configuration generators (Ninja, Makefiles) default to an optimized build.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# MinGW: link the C++ runtime into the .exe so it runs outside the compiler's own terminal.
if(MINGW)
  add_link_options(-static)
endif()

# The modules. Owners are listed in README.md.
add_library(hashmusic_core STATIC
  src/sha256.cpp       # Member A: SHA-256 from scratch
  src/mapper.cpp       # Member B: digest -> notes, tempo, velocity
  src/midi_writer.cpp  # Member C: notes -> Standard MIDI File bytes
  src/avalanche.cpp    # Member A: stretch goal, the avalanche comparison
  src/band.cpp         # extra from the proposal: instruments, harmony, bass, drums
  src/stats.cpp        # extra from the proposal: distribution charts
)
target_include_directories(hashmusic_core PUBLIC src)

# Member D: the command-line tool.
add_executable(hashmusic src/main.cpp)
target_link_libraries(hashmusic PRIVATE hashmusic_core)
if(WIN32)
  target_link_libraries(hashmusic PRIVATE shell32)  # CommandLineToArgvW, for UTF-8 arguments
endif()

# Task 6 (Member C): a hardcoded melody that exercises only the MIDI writer.
add_executable(hardcoded_melody tools/hardcoded_melody.cpp)
target_link_libraries(hardcoded_melody PRIVATE hashmusic_core)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
