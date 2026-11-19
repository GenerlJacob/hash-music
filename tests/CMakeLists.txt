# Tests (task 10, Member D). Run them all with:  ctest --test-dir build --output-on-failure

# Unit tests: one executable per module, sharing a tiny header-only harness (no downloads needed).
foreach(name sha256 mapper midi_writer pipeline)
  add_executable(test_${name} test_${name}.cpp test_main.cpp)
  target_link_libraries(test_${name} PRIVATE hashmusic_core)
  add_test(NAME unit_${name} COMMAND test_${name})
endforeach()

# Reads hex-encoded messages on stdin and prints their digests; used by the hashlib cross-check.
add_executable(sha256_batch sha256_batch.cpp)
target_link_libraries(sha256_batch PRIVATE hashmusic_core)

# Check "Determinism": the CLI writes byte-identical files for the same input, run after run.
add_test(NAME cli_determinism
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:hashmusic> -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/determinism
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_determinism.cmake)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  # Check "Cross-check": our SHA-256 matches Python's hashlib on 1,000 random inputs.
  add_test(NAME sha256_cross_check
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cross_check.py $<TARGET_FILE:sha256_batch> 1000)
  # Mapping cross-check: an independent Python model of the byte map agrees note for note with the CLI.
  add_test(NAME mapping_cross_check
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_mapping.py $<TARGET_FILE:hashmusic>
                   ${CMAKE_CURRENT_BINARY_DIR}/mapping)
  # Check "Playback": every generated file parses cleanly with an independent MIDI reader.
  add_test(NAME midi_playback_check
           COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:hashmusic> -DMELODY_EXE=$<TARGET_FILE:hardcoded_melody>
                   -DPYTHON=${Python3_EXECUTABLE} -DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/check_midi.py
                   -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/playback -P ${CMAKE_CURRENT_SOURCE_DIR}/playback_check.cmake)
else()
  message(WARNING "Python 3 not found: the hashlib and mapping cross-checks and the MIDI playback check will not run")
endif()
