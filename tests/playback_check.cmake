# Playback check: generate songs in every mode, then validate each file with check_midi.py, an
# independent Standard MIDI File reader.
# Run by CTest as: cmake -DEXE=... -DMELODY_EXE=... -DPYTHON=... -DCHECKER=... -DWORK_DIR=... -P playback_check.cmake
file(MAKE_DIRECTORY "${WORK_DIR}")

function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
  if(NOT result EQUAL 0)
    string(REPLACE ";" " " shown "${ARGN}")
    message(FATAL_ERROR "command failed (exit ${result}): ${shown}\n${output}")
  endif()
  message(STATUS "${output}")
endfunction()

run("${EXE}" -q -o "${WORK_DIR}/jacob.mid" jacob)
run("${EXE}" -q -o "${WORK_DIR}/jacob1.mid" jacob1)
run("${EXE}" -q --band -o "${WORK_DIR}/jacob_band.mid" jacob)
run("${EXE}" -q --band -o "${WORK_DIR}/fox_band.mid" "The quick brown fox jumps over the lazy dog")
run("${EXE}" -q --band --file "${CHECKER}" -o "${WORK_DIR}/file_theme.mid")
run("${EXE}" -q --compare jacob jacob1 -o "${WORK_DIR}/compare")
run("${MELODY_EXE}" "${WORK_DIR}/hardcoded.mid")

run("${PYTHON}" "${CHECKER}"
    "${WORK_DIR}/jacob.mid" "${WORK_DIR}/jacob1.mid" "${WORK_DIR}/jacob_band.mid" "${WORK_DIR}/fox_band.mid"
    "${WORK_DIR}/file_theme.mid" "${WORK_DIR}/compare_a.mid" "${WORK_DIR}/compare_b.mid"
    "${WORK_DIR}/hardcoded.mid")
